#include "Player.hpp"
#include <climits>
#include <filesystem>
#include <uuid.h>
#include <gst/app/gstappsink.h>
#include <picojson/picojson.h>
#include "../../lib/src/internal/Logging.hpp"
#include "mxl/flow.h"
#include "mxl/time.h"

namespace fs = std::filesystem;

namespace mxl::tools::gst
{

    Player::Player(std::string in_domain)
        : domain(std::move(in_domain))
    {
        // Create the MXL domain directory if it doesn't exist
        if (!fs::exists(domain))
        {
            try
            {
                fs::create_directories(domain);
                MXL_DEBUG("Created MXL domain directory: {}", domain);
            }
            catch (fs::filesystem_error const& e)
            {
                MXL_ERROR("Error creating domain directory: {}", e.what());
                throw;
            }
        }

        // Create the MXL SDK instance
        mxlInstance = mxlCreateInstance(domain.c_str(), nullptr);
        if (!mxlInstance)
        {
            throw std::runtime_error("Failed to create MXL instance");
        }
    }

    bool Player::open(std::string const& in_uri, int64_t in_inFrame, int64_t in_outFrame, bool in_loop)
    {
        //
        // Process the input file locator and convert local paths to URIs
        //
        auto scheme = g_uri_parse_scheme(in_uri.c_str());
        if (scheme != nullptr)
        {
            // Already a URI
            uri = in_uri;
        }
        else
        {
            // Local file path — convert to file:// URI and escape properly
            auto tmp = g_filename_to_uri(in_uri.c_str(), nullptr, nullptr);
            if (!tmp)
            {
                MXL_ERROR("Failed to convert file path to URI");
                return false;
            }
            uri = tmp;
            g_free(tmp);
        }

        MXL_DEBUG("Opening URI: {}", uri);
        inFrame = in_inFrame;
        outFrame = in_outFrame;
        loop = in_loop;

        //
        // Create the gstreamer pipeline
        // TODO it may be better to use filesrc instead of playbin which seems insist on playing audio on a sound device.
        //
        std::string pipelineDesc =
            "playbin uri=" + uri +
            " "
            "video-sink=\"appsink name=appSinkVideo emit-signals=false max-buffers=4 drop=false sync=true caps=video/x-raw,format=v210\" "
            "audio-sink=\"appsink name=appSinkAudio emit-signals=false max-buffers=10 drop=false sync=true "
            "caps=audio/x-raw,format=F32LE,rate=48000\"";

        GError* error = nullptr;
        pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
        if (!pipeline)
        {
            MXL_ERROR("Failed to create pipeline: {}", error->message);
            g_error_free(error);
            return false;
        }

        //
        // Pause the pipeline and wait for negotiation
        //
        auto bus = gst_element_get_bus(pipeline);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);

        bool negotiated = false;
        while (!negotiated)
        {
            GstMessage* in_msg = gst_bus_timed_pop_filtered(
                bus, GST_CLOCK_TIME_NONE, static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

            if (!in_msg)
            {
                continue;
            }

            switch (GST_MESSAGE_TYPE(in_msg))
            {
                case GST_MESSAGE_ASYNC_DONE: negotiated = true; break;
                case GST_MESSAGE_ERROR:      {
                    GError* err;
                    gchar* debug;
                    gst_message_parse_error(in_msg, &err, &debug);
                    MXL_ERROR("Pipeline error: {} ", err->message);
                    g_error_free(err);
                    g_free(debug);
                    break;
                }
                default: break;
            }
            gst_message_unref(in_msg);
        }
        gst_object_unref(bus);

        //
        // We should have completed negotiation by now
        //
        if (!negotiated)
        {
            MXL_ERROR("Failed to negotiate pipeline");
            return false;
        }

        // Retrieve the appsink elements once the pipeline built
        appSinkVideo = gst_bin_get_by_name(GST_BIN(pipeline), "appSinkVideo");

        // TODO not hooking up audio for now until Kimon's branch merges.
        // appSinkAudio = gst_bin_get_by_name(GST_BIN(pipeline), "appSinkAudio");
        if (!appSinkVideo && !appSinkAudio)
        {
            MXL_ERROR("No audio or video appsinks found");
            return false;
        }

        if (appSinkVideo != nullptr)
        {
            MXL_DEBUG("Creating MXL flow for video...");

            // Get negotiated caps from appsink's pad
            GstPad* pad = gst_element_get_static_pad(appSinkVideo, "sink");
            GstCaps* caps = gst_pad_get_current_caps(pad);
            int width = 0, height = 0, fps_n = 0, fps_d = 1;
            gchar const* interlace_mode = nullptr;
            gchar const* colorimetry = nullptr;

            if (caps)
            {
                GstStructure* s = gst_caps_get_structure(caps, 0);
                interlace_mode = gst_structure_get_string(s, "interlace-mode");
                colorimetry = gst_structure_get_string(s, "colorimetry");

                gst_structure_get_int(s, "width", &width);
                gst_structure_get_int(s, "height", &height);

                if (width <= 0 || height <= 0)
                {
                    MXL_ERROR("Invalid width or height in caps");
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (!gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d))
                {
                    MXL_ERROR("Failed to get framerate from caps");
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (fps_n <= 0 || fps_d <= 0)
                {
                    MXL_ERROR("Invalid framerate in caps {}/{}", fps_n, fps_d);
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }
                else if (fps_n == 0 && fps_d == 1)
                {
                    MXL_ERROR("Invalid framerate in caps {}/{}.  This potentially signals that the video stream is VFR (variable frame rate) which "
                              "is unsupported by this application.",
                        fps_n,
                        fps_d);
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (!interlace_mode)
                {
                    MXL_ERROR("Failed to get interlace mode from caps. Assuming progressive.");
                }

                if (!g_str_equal(interlace_mode, "progressive"))
                {
                    // TODO : Handle interlaced video
                    MXL_ERROR("Unsupported interlace mode.  Interpreting as progressive.");
                }

                // This assumes square pixels, bt709, sdr.  TODO read from caps.
                gst_caps_unref(caps);
            }
            else
            {
                MXL_ERROR("Failed to get caps from appsink pad");
                gst_object_unref(pad);
                return false;
            }

            gst_object_unref(pad);

            std::string flowDef;
            videoGrainRate = Rational{fps_n, fps_d};
            videoFlowId = createVideoFlowJson(in_uri, width, height, videoGrainRate, true, colorimetry, flowDef);

            FlowInfo flowInfo;
            auto res = mxlCreateFlow(mxlInstance, flowDef.c_str(), nullptr, &flowInfo);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow: {}", (int)res);
                return false;
            }

            res = mxlCreateFlowWriter(mxlInstance, uuids::to_string(videoFlowId).c_str(), nullptr, &flowWriterVideo);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow writer: {}", (int)res);
                return false;
            }

            MXL_INFO("Video flow : {}", uuids::to_string(videoFlowId));
        }

        if (appSinkAudio != nullptr)
        {
            MXL_INFO("Creating MXL flow for audio...");

            // Get negotiated caps from appsink's pad
            GstPad* pad = gst_element_get_static_pad(appSinkAudio, "sink");
            GstCaps* caps = gst_pad_get_current_caps(pad);
            int channels = 0;

            if (caps)
            {
                GstStructure* s = gst_caps_get_structure(caps, 0);
                if (gst_structure_has_field(s, "channels"))
                {
                    gst_structure_get_int(s, "channels", &channels);
                }
                else
                {
                    MXL_ERROR("Failed to get channels from caps");
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }
                gst_caps_unref(caps);
            }
            else
            {
                MXL_ERROR("Failed to get caps from appsink pad");
                gst_object_unref(pad);
                return false;
            }

            gst_object_unref(pad);

            std::string flowDef;
            audioFlowId = createAudioFlowJson(in_uri, channels, flowDef);
            MXL_INFO("Audio flow : {}", uuids::to_string(audioFlowId));

            FlowInfo flowInfo;
            auto res = mxlCreateFlow(mxlInstance, flowDef.c_str(), nullptr, &flowInfo);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create sound flow: {}", (int)res);
                return false;
            }

            res = mxlCreateFlowWriter(mxlInstance, uuids::to_string(audioFlowId).c_str(), nullptr, &flowWriterAudio);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow writer: {}", (int)res);
                return false;
            }
        }

        // TODO. Make seeking work.
        // Seek to the in-frame if specified
        if (inFrame > 0)
        {
            MXL_DEBUG("Seeking to: {}", inFrame);
            if (!gst_element_seek(pipeline,
                    1.0,
                    GST_FORMAT_DEFAULT,
                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                    GST_SEEK_TYPE_SET,
                    inFrame,
                    GST_SEEK_TYPE_NONE,
                    GST_CLOCK_TIME_NONE))
            {
                MXL_ERROR("Failed to seek to: {}", inFrame);
            }
            else
            {
                currentFrame = inFrame;
            }
        }

        return true;
    }

    bool Player::start()
    {
        //
        // Start the pipeline
        //
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        running = true;

        //
        // Create the audio and video threads to pull samples from the appsinks
        //
        videoThreadPtr = appSinkVideo ? std::make_unique<std::thread>(&Player::videoThread, this) : nullptr;
        audioThreadPtr = appSinkAudio ? std::make_unique<std::thread>(&Player::audioThread, this) : nullptr;

        return true;
    }

    void Player::stop()
    {
        running = false;
    }

    Player::~Player()
    {
        // Join threads if they were created
        if (videoThreadPtr && videoThreadPtr->joinable())
        {
            videoThreadPtr->join();
        }
        if (audioThreadPtr && audioThreadPtr->joinable())
        {
            audioThreadPtr->join();
        }

        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);

        if (mxlInstance)
        {
            if (flowWriterVideo)
            {
                mxlDestroyFlowWriter(mxlInstance, flowWriterVideo);
                auto id = uuids::to_string(videoFlowId);
                mxlDestroyFlow(mxlInstance, id.c_str());
            }

            if (flowWriterAudio)
            {
                mxlDestroyFlowWriter(mxlInstance, flowWriterAudio);
                auto id = uuids::to_string(audioFlowId);
                mxlDestroyFlow(mxlInstance, id.c_str());
            }
            mxlDestroyInstance(mxlInstance);
        }
    }

    uuids::uuid Player::createVideoFlowJson(std::string const& in_uri, int in_width, int in_height, Rational in_rate, bool in_progressive,
        std::string const& in_colorspace, std::string& out_flowDef)
    {
        picojson::object root;

        std::string label = "Video flow for " + in_uri;

        root["description"] = picojson::value(label);

        auto id = uuids::uuid_system_generator{}();
        root["id"] = picojson::value(uuids::to_string(id));
        root["tags"] = picojson::value(picojson::object());
        root["format"] = picojson::value("urn:x-nmos:format:video");
        root["label"] = picojson::value(label);
        root["parents"] = picojson::value(picojson::array());
        root["media_type"] = picojson::value("video/v210");

        picojson::object grain_rate;
        grain_rate["numerator"] = picojson::value(static_cast<double>(in_rate.numerator));
        grain_rate["denominator"] = picojson::value(static_cast<double>(in_rate.denominator));
        root["grain_rate"] = picojson::value(grain_rate);

        root["frame_width"] = picojson::value(static_cast<double>(in_width));
        root["frame_height"] = picojson::value(static_cast<double>(in_height));
        root["interlace_mode"] = picojson::value(in_progressive ? "progressive" : "interlaced_tff"); // todo. handle bff.
        root["colorspace"] = picojson::value(in_colorspace);

        picojson::array components;
        auto add_component = [&](std::string const& name, int w, int h)
        {
            picojson::object comp;
            comp["name"] = picojson::value(name);
            comp["width"] = picojson::value(static_cast<double>(w));
            comp["height"] = picojson::value(static_cast<double>(h));
            comp["bit_depth"] = picojson::value(10.0);
            components.emplace_back(comp);
        };

        add_component("Y", in_width, in_height);
        add_component("Cb", in_width / 2, in_height);
        add_component("Cr", in_width / 2, in_height);

        root["components"] = picojson::value(components);

        out_flowDef = picojson::value(root).serialize(true);
        return id;
    }

    uuids::uuid Player::createAudioFlowJson(std::string const& in_uri, int in_channelCount, std::string& out_flowDef)
    {
        picojson::object root;

        std::string label = "Sound flow for " + in_uri;
        auto id = uuids::uuid_system_generator{}();
        root["id"] = picojson::value(uuids::to_string(id));
        root["description"] = picojson::value(label);
        root["format"] = picojson::value("urn:x-nmos:format:audio");
        root["tags"] = picojson::value(picojson::object());
        root["label"] = picojson::value(label);
        root["media_type"] = picojson::value("audio/float32");

        picojson::object sample_rate;
        sample_rate["numerator"] = picojson::value(48000.0);
        root["sample_rate"] = picojson::value(sample_rate);
        root["channel_count"] = picojson::value(static_cast<double>(in_channelCount));
        root["bit_depth"] = picojson::value(32.0);
        root["parents"] = picojson::value(picojson::array());

        picojson::object grain_rate;
        grain_rate["numerator"] = picojson::value(static_cast<double>(100));
        grain_rate["denominator"] = picojson::value(static_cast<double>(1));
        root["grain_rate"] = picojson::value(grain_rate);

        out_flowDef = picojson::value(root).serialize(true);
        return id;
    }

    void Player::videoThread()
    {
        while (running)
        {
            //
            // TODO This loop is very naive.  it relies on gstreamer for clocking (see pipeline string 'synced' attributes) it could instead consume
            // as fast as possible and then use mxl functions to sleep until the next grain boundary.  this would be more accurate.
            //
            auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appSinkVideo), 100'000'000);
            if (!sample)
            {
                // FIX ME.  this should properly listen to EOS on the bus instead of relying on arbitrary timeouts
                MXL_DEBUG("End of video stream reached.\n");
                running = false;
                continue;
            }

            uint64_t grainIndex = mxlGetCurrentGrainIndex(&videoGrainRate);
            if (lastVideoGrainIndex == 0)
            {
                lastVideoGrainIndex = grainIndex;
            }
            else if (grainIndex != lastVideoGrainIndex + 1)
            {
                MXL_WARN("Video skipped grain index. Expected {}, got {}", lastVideoGrainIndex + 1, grainIndex);
            }

            lastVideoGrainIndex = grainIndex;

            // Sleep until the next grain boundary, in case gstreamer gives us samples too fast
            auto ns = mxlGetNsUntilGrainIndex(grainIndex, &videoGrainRate);
            if (ns > 0)
            {
                mxlSleepForNs(ns);
            }

            int64_t frame = currentFrame++;
            if (loop && outFrame >= 0 && frame >= outFrame)
            {
                // Seeking does not seem to work at all.
                MXL_DEBUG("Outpoint reached. Seeking to: {}", inFrame);
                if (gst_element_seek(pipeline,
                        1.0,
                        GST_FORMAT_DEFAULT,
                        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                        GST_SEEK_TYPE_SET,
                        inFrame,
                        GST_SEEK_TYPE_NONE,
                        GST_CLOCK_TIME_NONE))
                {
                    currentFrame = inFrame;
                }
                else
                {
                    MXL_ERROR("Failed to seek to in-frame: {}", inFrame);
                }
                gst_sample_unref(sample);
                continue;
            }

            auto buffer = gst_sample_get_buffer(sample);
            if (buffer)
            {
                GstClockTime pts = GST_BUFFER_PTS(buffer);
                if (GST_CLOCK_TIME_IS_VALID(pts))
                {
                    MXL_TRACE("Video frame received.  Frame {}, pts (ms) {}, duratio (ms) {}",
                        frame,
                        pts / GST_MSECOND,
                        GST_BUFFER_DURATION(buffer) / GST_MSECOND);
                }
            }

            GstMapInfo map;
            if (gst_buffer_map(buffer, &map, GST_MAP_READ))
            {
                /// Open the grain.
                GrainInfo gInfo;
                uint8_t* mxl_buffer = nullptr;

                /// Open the grain for writing.
                if (mxlFlowWriterOpenGrain(mxlInstance, flowWriterVideo, grainIndex, &gInfo, &mxl_buffer) != MXL_STATUS_OK)
                {
                    MXL_ERROR("Failed to open grain at index '{}'", grainIndex);
                    break;
                }

                gInfo.commitedSize = map.size;
                ::memcpy(mxl_buffer, map.data, map.size);

                if (mxlFlowWriterCommit(mxlInstance, flowWriterVideo, &gInfo) != MXL_STATUS_OK)
                {
                    MXL_ERROR("Failed to open grain at index '{}'", grainIndex);
                    break;
                }

                gst_buffer_unmap(buffer, &map);
            }

            gst_sample_unref(sample);
        }
    }

    void Player::audioThread()
    {
        while (running)
        {
            // TODO. fixme.   this is very arbitrary.  We should be listening to the bus for EOS messages
            auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appSinkAudio), 100'000'000);
            if (sample)
            {
                auto buffer = gst_sample_get_buffer(sample);
                auto caps = gst_sample_get_caps(sample);

                gint channels = 0;
                if (caps)
                {
                    auto s = gst_caps_get_structure(caps, 0);
                    gst_structure_get_int(s, "channels", &channels);
                }

                GstMapInfo map;
                if (gst_buffer_map(buffer, &map, GST_MAP_READ))
                {
                    // float const* audioData = reinterpret_cast<float const*>(map.data);
                    // size_t numSamples = map.size / sizeof(float);
                    // frames = channels > 0 ? numSamples / channels : 0;

                    // Process float32 audio samples here (interleaved, with known channels and frames)
                    //("Processing audio frame of size: {} with {} channels and {} frames", map.size, channels, frames);
                    gst_buffer_unmap(buffer, &map);
                }
                gst_sample_unref(sample);
            }
        }
    }

}
