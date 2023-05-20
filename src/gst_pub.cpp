#include "gstreamer_image_transport/gst_pub.hpp"

#include <bits/chrono.h>
#include <bits/types/struct_timespec.h>
#include <cstdint>
#include <memory>
#include <chrono>
#include <span>
#include <rclcpp/time.hpp>
#include <sensor_msgs/msg/detail/image__struct.hpp>
#include <stdexcept>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <locale>

#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sys/types.h>

#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include "gst/app/gstappsink.h"
#include "gst/gstbuffer.h"
#include "gst/gstcaps.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstinfo.h"
#include "gst/gstmemory.h"
#include "gst/gstobject.h"
#include "gst/gstpad.h"
#include "gst/gstpipeline.h"
#include "gst/gstsample.h"
#include "gst/gstsegment.h"
#include "gstreamer_image_transport/msg/detail/data_packet__struct.hpp"
#include "rclcpp/logging.hpp"

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

namespace {
// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

};

GStreamerPublisher::GStreamerPublisher() :
    _logger(rclcpp::get_logger("gst_pub")),
    _gst_pipeline_str(""),
    _gst_pipeline(nullptr),
    _gst_src(nullptr),
    _gst_sink(nullptr)
{
    //TODO: ???
}

bool GStreamerPublisher::gst_configure() {
    RCLCPP_INFO_STREAM(_logger, "Initializing gstreamer...");
    gst_init(nullptr, nullptr);
    RCLCPP_INFO_STREAM(_logger, "GStreamer Version: " << gst_version_string() );


    RCLCPP_INFO_STREAM(_logger, "Parsing pipeline...");
    GError * error = nullptr;  // Assignment to zero is a gst requirement
    _gst_pipeline = GST_BIN(gst_parse_bin_from_description(_gst_pipeline_str.c_str(), false, &error));

    if ((_gst_pipeline == nullptr) || error) {
        RCLCPP_FATAL_STREAM(_logger, "Error parsing pipeline: " << (error ? error->message : "unknown error"));
        return false;
    }

    // if (!GST_IS_PIPELINE(_gst_pipeline)) {
    //     RCLCPP_FATAL_STREAM(_logger, "Invalid pipeline!");
    //     raise std::runtime_error("Invalid pipeline!");
    // }

    // Create IO
    RCLCPP_INFO_STREAM(_logger, "Implementing pipeline...");
    _gst_src = GST_APP_SRC(gst_element_factory_make("appsrc", nullptr));
    _gst_sink = GST_APP_SINK(gst_element_factory_make("appsink", nullptr));


    g_object_set(
        G_OBJECT(_gst_src),
        "stream-type", GST_APP_STREAM_TYPE_STREAM,
        "is-live", true
    );

    //XXX:  Pipeline, src, and sink will get cleaned up later by 'gst_clean_up()'
    //      Everything else here needs to be deref'd properly

    auto in_pad = gst_bin_find_unlinked_pad(GST_BIN(_gst_pipeline), GST_PAD_SINK);
    auto out_pad = gst_bin_find_unlinked_pad(GST_BIN(_gst_pipeline), GST_PAD_SRC);
    if((in_pad == nullptr) || (out_pad == nullptr)) {
        RCLCPP_FATAL_STREAM(
            _logger,
            "Error in pipeline structure: missing IO pads ("
            << ((in_pad == nullptr) ? "sink" : "")
            << ((in_pad == nullptr) && (out_pad == nullptr) ? ", " : "")
            << ((out_pad == nullptr) ? "source" : "")
            << ")"
        );

        if(in_pad) gst_object_unref(in_pad);
        if(out_pad) gst_object_unref(out_pad);
        return false;
    }

    auto in_element = gst_pad_get_parent_element(in_pad);
    auto out_element = gst_pad_get_parent_element(out_pad);
    gst_object_unref(in_pad);
    gst_object_unref(out_pad);
    if((in_element == nullptr) || (out_element == nullptr)) {
        RCLCPP_FATAL_STREAM(
            _logger,
            "Error in pipeline structure: pads missing parent elements ("
            << ((in_element == nullptr) ? "sink" : "")
            << ((in_element == nullptr) && (out_element == nullptr) ? ", " : "")
            << ((out_element == nullptr) ? "source" : "")
            << ")"
        );

        if(in_element) gst_object_unref(in_element);
        if(out_element) gst_object_unref(out_element);
        return false;
    }

    if (!gst_bin_add(GST_BIN(_gst_pipeline), GST_ELEMENT(_gst_src)) || !gst_bin_add(GST_BIN(_gst_pipeline), GST_ELEMENT(_gst_sink))) {
        RCLCPP_FATAL(_logger, "gst_bin_add() failed");
        gst_object_unref(in_element);
        gst_object_unref(out_element);
        return false;
    }

    if (!gst_element_link(GST_ELEMENT(_gst_src), in_element)) {
        RCLCPP_FATAL_STREAM(
            _logger, "Error creating link: source -> \"" << gst_element_get_name(in_element) << "\""
        );

        gst_object_unref(in_element);
        gst_object_unref(out_element);
        return false;
    }

    if (!gst_element_link(out_element, GST_ELEMENT(_gst_sink))) {
        RCLCPP_FATAL_STREAM(
            _logger, "Error creating link:  \"" << gst_element_get_name(out_element) << "\" -> sink"
        );

        gst_object_unref(in_element);
        gst_object_unref(out_element);
        return false;
    }

    gst_object_unref(in_element);
    gst_object_unref(out_element);


    RCLCPP_INFO_STREAM(_logger, "Initializing stream...");

    if(get_pipeline_state(1s) == GST_STATE_NULL) {
        RCLCPP_INFO_STREAM(_logger, "Setting stream ready...");
        if (gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
            const auto msg = "Could not initialise stream!";
            RCLCPP_FATAL_STREAM(_logger, msg);
            return false;
        }
    }

    if(get_pipeline_state(1s) == GST_STATE_READY) {
        RCLCPP_INFO_STREAM(_logger, "Setting stream paused...");
        if (gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
            const auto msg = "Could not pause stream!";
            RCLCPP_FATAL_STREAM(_logger, msg);
            return false;
        }
    }

    // if(get_pipeline_state(1s) == GST_STATE_PAUSED) {
    //     RCLCPP_INFO_STREAM(_logger, "Setting stream playing...");
    //     if (gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    //         const auto msg = "Could not start stream!";
    //         RCLCPP_FATAL_STREAM(_logger, msg);
    //         return false;
    //     }
    // }

    return get_pipeline_state(1s) == GST_STATE_PAUSED;
}

void GStreamerPublisher::gst_clean_up() {
    if(_has_shutdown)
        return;

    // RCLCPP_INFO(_logger, "Shutting down...");

    if(_gst_pipeline) {
        gst_element_set_state (GST_ELEMENT(_gst_pipeline), GST_STATE_PAUSED);
        gst_element_set_state (GST_ELEMENT(_gst_pipeline), GST_STATE_READY);
        gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_NULL);
        gst_object_unref(_gst_pipeline);
        _gst_pipeline = nullptr;

        //XXX: Pipeline will clear up all other references as well
        if(_gst_src != nullptr) _gst_src = nullptr;
        if(_gst_sink != nullptr) _gst_sink = nullptr;
    }

    if(_gst_src != nullptr) {
        gst_object_unref(_gst_src);
        _gst_src = nullptr;
    }

    if(_gst_sink != nullptr) {
        gst_object_unref(_gst_sink);
        _gst_sink = nullptr;
    }

    // RCLCPP_INFO(_logger, "Deinit GST...");
    // gst_deinit();
    // RCLCPP_INFO(_logger, "Done!");

    _has_shutdown = true;
}

GstState GStreamerPublisher::get_pipeline_state(std::chrono::system_clock::duration timeout) const {
    GstState current_state;
    GstState pending_state;

    GstClockTime gt = GST_CLOCK_TIME_NONE;

    if(timeout > std::chrono::system_clock::duration::zero()) {
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - std::chrono::seconds(seconds)).count();
        timespec ts {seconds, nanoseconds};
        gt = GST_TIMESPEC_TO_TIME(ts);
    }

    const auto success = gst_element_get_state(GST_ELEMENT(_gst_pipeline), &current_state, &pending_state, gt);
    if(success == GST_STATE_CHANGE_FAILURE) {
        RCLCPP_FATAL_STREAM(_logger, "Last stream state change failed (" << success << ")!");
        throw std::runtime_error("Last stream state change failed!");
    }

    return success == GST_STATE_CHANGE_SUCCESS ? current_state : pending_state;
}


void GStreamerPublisher::advertiseImpl(rclcpp::Node * node, const std::string & base_topic, rmw_qos_profile_t custom_qos) {
    //Do implicit advertising
    SimplePublisherPlugin::advertiseImpl(node, base_topic, custom_qos);

    // Get GStreamer parameters
    _gst_pipeline_str = node->declare_parameter("pipeline", "rawvideoparse");

    //Get the last step in the encoder
    const auto int_split = _gst_pipeline_str.rfind("!");
    _encoder = _gst_pipeline_str.substr(int_split == std::string::npos ? 0 : int_split);
    trim(_encoder);

    RCLCPP_INFO_STREAM(_logger, "Using pipeline: \"" << _gst_pipeline_str << "\"");
    RCLCPP_INFO_STREAM(_logger, "Encoder hint: \"" << _encoder << "\"");

    if(!gst_configure()) {
        gst_clean_up();
        const auto msg = "Unable to configure GStreamer";
        RCLCPP_FATAL_STREAM(_logger, msg);
        throw std::runtime_error(msg);
    }

    RCLCPP_INFO(_logger, "Started stream!");
}

GStreamerPublisher::~GStreamerPublisher() {
    gst_clean_up();
}

GstCaps* GStreamerPublisher::get_caps(const sensor_msgs::msg::Image& image) const {
    GstCaps* caps = nullptr;
    const auto frame_it = ros_gst_frame_encodings.find(image.encoding);
    if(frame_it != ros_gst_frame_encodings.end()) {
        caps = gst_caps_new_simple(
            "video/x-raw",
            "format", G_TYPE_STRING, frame_it->second.data(), nullptr
        );
    } else {
        const auto image_it = ros_gst_image_encodings.find(image.encoding);
        if(image_it != ros_gst_image_encodings.end()) {
            caps = gst_caps_new_simple(image_it->second.data(), nullptr, nullptr);
        }
    }

    return caps;
}

void GStreamerPublisher::publish(
  const sensor_msgs::msg::Image & message,
  const PublishFn & publish_fn) const
{
    if(getNumSubscribers() <= 0) {
        return;
    }

    //Push data into the pipeline

    // GstPad * src_pad = gst_element_get_static_pad(_gst_src, "src");
    // const GstCaps * src_caps = gst_pad_get_current_caps(src_pad);
    // GstStructure * src_structure = gst_caps_get_structure(src_caps, 0);
    // gint current_width;
    // gint current_height;
    // gst_structure_get_int(src_structure, "width", &current_width);
    // gst_structure_get_int(src_structure, "height", &current_height);

    //Check if we need to reconfigure the input caps
    // if( (_current_width != message.width) ||
    //     (_current_height != message.height) ||
    //     (_current_encoding != message.encoding) ) {

    //     // _current_width = message.width;
    //     // _current_height = message.height;
    //     // _current_encoding = message.encoding;
    // }

    auto caps = get_caps(message);
    if(caps == nullptr) {
        RCLCPP_ERROR_STREAM(_logger,
            "Unable to reconfigure to format: " << message.encoding <<
            "(" << message.width << "x" << message.height << ")"
        );

        return;
    }

    const size_t data_size = message.data.size()*sizeof(sensor_msgs::msg::Image::_data_type::value_type);
    auto mem_in = gst_memory_new_wrapped(
        GstMemoryFlags::GST_MEMORY_FLAG_READONLY, // | GstMemoryFlags::GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS
        (gpointer)message.data.data(),
        data_size, 0, data_size,
        nullptr, nullptr
    );

    GstBuffer* buffer_in = gst_buffer_new();
    gst_buffer_append_memory(buffer_in, mem_in);
    timespec ts {message.header.stamp.sec, message.header.stamp.nanosec};
    gst_buffer_add_reference_timestamp_meta(buffer_in, caps, GST_TIMESPEC_TO_TIME(ts), GST_CLOCK_TIME_NONE);

    auto sample_in = gst_sample_new(buffer_in, caps, nullptr, nullptr);
    if(gst_app_src_push_sample(GST_APP_SRC(_gst_src), sample_in) != GST_FLOW_OK) {
        RCLCPP_ERROR(_logger, "Could not push sample, frame dropped");
        return;
    }
    gst_buffer_unref(buffer_in);
    gst_sample_unref(sample_in);

    //Pull sample back out of pipeline
    GstSample * sample_out = gst_app_sink_pull_sample(GST_APP_SINK(_gst_sink));
    if (!sample_out) {
        RCLCPP_ERROR(_logger, "Could not get gstreamer sample.");
        if(gst_app_sink_is_eos(_gst_sink))
            RCLCPP_ERROR(_logger, "End-of-stream is in place!");

        if(get_pipeline_state(10ms) != GST_STATE_READY)
            RCLCPP_ERROR(_logger, "Stream is not ready!");

        return;
    }

    GstBuffer * buffer_out = gst_sample_get_buffer(sample_out);
    GstMemory * memory_out = gst_buffer_get_memory(buffer_out, 0);
    GstMapInfo info;
    gst_memory_map(memory_out, &info, GST_MAP_READ);
    const auto data_out = std::span(info.data, info.size);

    gstreamer_image_transport::msg::DataPacket packet;
    packet.header = message.header;
    packet.encoder = _encoder;
    packet.data.assign(data_out.begin(), data_out.end());

    publish_fn(packet);

    gst_memory_unref(memory_out);
    gst_buffer_unref(buffer_out);
    gst_sample_unref(sample_out);

//   cv::Mat cv_image;
//   std::shared_ptr<void const> tracked_object;
//   try {
//     cv_image = cv_bridge::toCvShare(message, tracked_object, message.encoding)->image;
//   } catch (const cv::Exception & e) {
//     auto logger = rclcpp::get_logger("gst_pub");
//     RCLCPP_ERROR(
//       logger, "Could not convert from '%s' to '%s'.",
//       message.encoding.c_str(), message.encoding.c_str());
//     return;
//   }

//   // Rescale image
//   double subsampling_factor = 2.0;
//   int new_width = cv_image.cols / subsampling_factor + 0.5;
//   int new_height = cv_image.rows / subsampling_factor + 0.5;
//   cv::Mat buffer;
//   cv::resize(cv_image, buffer, cv::Size(new_width, new_height));

//   // Set up ResizedImage and publish
//   image_transport_tutorials::msg::ResizedImage resized_image;
//   resized_image.original_height = cv_image.rows;
//   resized_image.original_width = cv_image.cols;
//   resized_image.image = *(cv_bridge::CvImage(message.header, "bgr8", cv_image).toImageMsg());
//   publish_fn(resized_image);
}

};