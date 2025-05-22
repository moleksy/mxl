#pragma once

#include <csignal>
#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <uuid.h>
#include <gst/gst.h>
#include <gst/gstelement.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>

namespace mxl::tools::gst
{
    class Player
    {
    public:
        ///
        /// Player constructor.
        /// Note: Uses gstreamer.  Assumes gstreamer is already initialized
        ///
        /// \param in_domain The MXL domain
        Player(std::string in_domain);

        ~Player();

        ///
        /// Opens an URI
        ///
        /// \param in_uri The URI to play
        /// \param in_inFrame The first frame to play
        /// \param in_outFrame The last frame to play
        /// \param in_loop Whether to loop the video
        /// \return True if successful, false otherwise
        bool open(std::string const& in_uri, int64_t in_inFrame, int64_t in_outFrame, bool in_loop);

        // Starts the player
        [[nodiscard]]
        bool start();

        // Stops the player
        void stop();

        [[nodiscard]]
        bool isRunning() const
        {
            return running;
        }

    private:
        //
        // Create a JSON Video Flow definition suitable for MXL
        //
        // \param in_uri The input URI
        // \param in_width The width of the video
        // \param in_height The height of the video
        // \param in_rate The video grain rate
        // \param in_progressive Whether the video is progressive
        // \param in_colorspace The colorspace of the video
        // \param out_flowDef The output flow definition JSON string
        //
        // \return The flow ID
        //
        static uuids::uuid createVideoFlowJson(std::string const& in_uri, int in_width, int in_height, Rational in_rate, bool in_progressive,
            std::string const& in_colorspace, std::string& out_flowDef);

        //
        // Create a JSON Audio Flow definition suitable for MXL
        //
        // \param in_uri The input URI
        // \param in_channelCount The number of audio channels
        // \param out_flowDef The output flow definition JSON string
        //
        // \return The flow ID
        //
        static uuids::uuid createAudioFlowJson(std::string const& in_uri, int in_channelCount, std::string& out_flowDef);

        //
        // Video processing thread entry point. Consumes samples from the videoappsink
        //
        void videoThread();

        //
        // Audio processing thread entry point. Consumes samples from the audioappsink
        //
        void audioThread();

        // The URI GST PlayBin will use to play the video
        std::string uri;
        // The MXL video flow id
        uuids::uuid videoFlowId;
        // The MXL audio flow id
        uuids::uuid audioFlowId;
        // Unique pointers to video and audio processing threads
        std::unique_ptr<std::thread> videoThreadPtr, audioThreadPtr;

        // The MXL domain
        std::string domain;
        // Video flow writer allocated by the MXL instance
        ::mxlFlowWriter flowWriterVideo = nullptr;
        // Audio flow writer allocated by the MXL instance
        ::mxlFlowWriter flowWriterAudio = nullptr;
        // The MXL instance
        ::mxlInstance mxlInstance = nullptr;

        ::GstElement* pipeline = nullptr;
        ::GstElement* appSrc = nullptr;
        ::GstElement* appSinkVideo = nullptr;
        ::GstElement* appSinkAudio = nullptr;

        // First frame to play
        int64_t inFrame = 0;
        // Last frame to play
        int64_t outFrame = -1; // -1 means no out frame set
        // Loop from inFrame to outFrame if set.  Loops from frame 0 until defined if not set.
        bool loop = false;
        // Keep a copy of the last video grain index
        uint64_t lastVideoGrainIndex = 0;
        // Keep a copy of the last audio grain index
        uint64_t lastAudioGrainIndex = 0;
        // Running flag
        std::atomic<bool> running{false};
        // Current frame number
        std::atomic<int64_t> currentFrame{0};
        // The video grain rate
        ::Rational videoGrainRate{0, 1};
    };

} // namespace mxl::tools::gst
