#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <rcl/time.h>
#include <rclcpp/context.hpp>
#include <rclcpp/utilities.hpp>
#include <string>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <string_view>


#include <gst/gstpipeline.h>
#include "gst/gstcaps.h"
#include "gst/gstcapsfeatures.h"
#include "gst/gstclock.h"
#include "gst/gstmeta.h"
#include "gst/gstpad.h"
#include "gst/gststructure.h"
#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"

#include <rclcpp/logger.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

using ReceiveSample = std::function<bool(GstSample* sample)>;

struct gstreamer_context_data {
    // GMainContext* _context;
    // GMainLoop* loop = nullptr;
    GstPipeline *pipeline = nullptr;
    GstAppSrc *source = nullptr;
    GstAppSink *sink = nullptr;
    std::atomic<bool> feed_open = false;

    //XXX: These should be set before use
    ReceiveSample receive_sample;
    rclcpp::Logger* logger = nullptr;
};

// using StartFeedCB = std::function<void(GstElement, guint, gstreamer_context_data)>;
// using StopFeedCB = std::function<void(GstElement, gstreamer_context_data)>;
// using SampleReadyCB = std::function<GstFlowReturn(GstElement, GstMessage , gstreamer_context_data)>;
// using ErrorCB = std::function<void(GstBus, GstMessage, gstreamer_context_data)>;

static void start_feed([[maybe_unused]]GstElement *source, [[maybe_unused]] guint size, gstreamer_context_data *context) {
    context->feed_open = true;
    // if(context->logger) RCLCPP_WARN(*(context->logger), "START");
}

static void stop_feed([[maybe_unused]]GstElement *source, gstreamer_context_data *context) {
    context->feed_open = false;
    // if(context->logger) RCLCPP_WARN(*(context->logger), "STOP");
}

// static GstFlowReturn sample_ready(GstElement *sink, gstreamer_context_data *context) {
//     GstSample *sample;
//     GstFlowReturn flow = GST_FLOW_ERROR;
//     if(context->logger) RCLCPP_WARN(*(context->logger), "PULL");
//     // Retrieve the buffer
//     g_signal_emit_by_name (sink, "pull-sample", &sample);
//     if (sample) {
//         if(context->receive_sample(sample))
//             flow = GST_FLOW_OK;

//         gst_sample_unref (sample);
//     }

//     return flow;
// }

static void error_cb([[maybe_unused]] GstBus *bus, GstMessage *msg, gstreamer_context_data *context) {
    GError *err;
    gchar *debug_info;

    /* Print error details on the screen */
    gst_message_parse_error (msg, &err, &debug_info);
    g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
    g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
    if(context->logger) {
        RCLCPP_FATAL_STREAM(*(context->logger), "Error received from element " << GST_OBJECT_NAME (msg->src) << ": " << err->message);
        RCLCPP_FATAL_STREAM(*(context->logger), "Debugging information: " << (debug_info ? debug_info : "none"));
    }
    g_clear_error (&err);
    g_free (debug_info);

    // g_main_loop_quit (context->loop);

    if(context->pipeline) {
        gst_element_set_state(GST_ELEMENT(context->pipeline), GST_STATE_NULL);
    }
    rclcpp::shutdown(nullptr, "Critical GStreamer error");
}



namespace encoding {

const std::unordered_map<std::string_view, std::string_view> ros_gst_frame {{
    {sensor_msgs::image_encodings::RGB8, "RGB"},
    {sensor_msgs::image_encodings::MONO8, "GRAY8"},
    {sensor_msgs::image_encodings::YUV422, "UYVY"},
    {sensor_msgs::image_encodings::YUV422_YUY2, "YUY2"},
}};

const std::unordered_map<std::string_view, std::string_view> ros_gst_image {{
    {"jpeg", "image/jpeg"},
    {"jpg", "image/jpeg"},
    {"png", "image/png"},
}};

const auto caps_name = "name";

const auto info_meta_name = "ros-frame";
const auto info_meta_frame_id = "id";
const auto info_meta_stamp = "stamp";

const auto info_reference = gst_caps_from_string("timestamp/x-ros-camera-stream");

};

namespace common {

const auto duration_zero = rclcpp::Duration(0, 0);
const auto time_zero = rclcpp::Time(0, 0, RCL_ROS_TIME);

constexpr std::string appsrc_name = "source";
constexpr std::string appsink_name = "sink";
constexpr std::string pipeline_split = "!";
constexpr std::string pipeline_split_spaced = " ! ";
constexpr std::string transport_name = "gst";

inline std::string get_topic(const std::string& base_topic, const std::string& transport_name) {
    return base_topic + "/" + transport_name;
}

inline GstClockTime ros_time_to_gst(const rclcpp::Duration& t) {
    return GST_NSECOND*t.nanoseconds();
}

// trim from start (in place)
inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
inline void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

// trim from both ends (copying)
inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

inline std::string get_appsrc_str(const std::string_view name = appsrc_name) {
    return std::string("appsrc name=").append(name);//.append(configuration::pipeline_split_spaced).append(std::string("queue"));
}

inline std::string get_appsink_str(const std::string_view name = appsink_name) {
    return std::string("queue").append(pipeline_split_spaced).append("appsink name=").append(name);
}

inline GstState get_pipeline_state(GstPipeline* pipeline, std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero()) {
    GstState current_state;
    // GstState pending_state;

    GstClockTime gt = GST_CLOCK_TIME_NONE;

    if(timeout > std::chrono::system_clock::duration::zero()) {
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
        gt = GST_NSECOND*nanoseconds.count();
    }

    // const auto success = gst_element_get_state(GST_ELEMENT(_gst_pipeline), &current_state, &pending_state, gt);
    gst_element_get_state(GST_ELEMENT(pipeline), &current_state, nullptr, gt);
    // if(success == GST_STATE_CHANGE_) {
    //     RCLCPP_FATAL_STREAM(_logger, "Last stream state change failed (" << success << ")!");
    // }

    // return success == GST_STATE_CHANGE_SUCCESS ? current_state : pending_state;
    return current_state;
}

inline void gst_do_init(const rclcpp::Logger logger) {
    RCLCPP_INFO_STREAM(logger, "Initializing gstreamer...");
    gst_init(nullptr, nullptr);
    RCLCPP_INFO_STREAM(logger, "GStreamer Version: " << gst_version_string() );
}

inline void gst_set_debug_level(const int64_t debug_level) {
    if(debug_level >= 0) {
        GstDebugLevel level = (debug_level >= GstDebugLevel::GST_LEVEL_MEMDUMP) ? GstDebugLevel::GST_LEVEL_MEMDUMP : static_cast<GstDebugLevel>(debug_level);
        gst_debug_set_active(level != GstDebugLevel::GST_LEVEL_NONE);
        gst_debug_set_default_threshold(level);
        // if (gst_debug_is_active()) {
        //     GstDebugLevel level = gst_debug_get_default_threshold();
        //     if (level < GST_LEVEL_ERROR) {
        //         level = GST_LEVEL_ERROR;
        //     }
        // }
    }
}

//XXX: This should always be called after `gst_do_init()`
inline bool gst_configure(const rclcpp::Logger logger, const std::string pipeline_internal, gstreamer_context_data& context) {
    //Append our parts to the pipeline
    const auto pipeline_str = get_appsrc_str()
                            + (pipeline_internal.empty() ? "" : pipeline_split_spaced + pipeline_internal)
                            + pipeline_split_spaced
                            + get_appsink_str();
    RCLCPP_INFO_STREAM(logger, "Using pipeline: \"" << pipeline_str << "\"");


    RCLCPP_INFO_STREAM(logger, "Parsing pipeline...");
    GError * error = nullptr;  // Assignment to zero is a gst requirement
    const auto try_pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

    if (!GST_IS_PIPELINE(try_pipeline) || error) {
        RCLCPP_FATAL_STREAM(logger, "Error parsing pipeline: " << (error ? error->message : "unknown error"));
        return false;
    }

    context.pipeline = GST_PIPELINE(try_pipeline);

    const auto appsrc = gst_bin_get_by_name(GST_BIN(context.pipeline), appsrc_name.c_str());
    const auto appsink = gst_bin_get_by_name(GST_BIN(context.pipeline), appsink_name.c_str());

    const auto src_ok = GST_IS_APP_SRC(appsrc);
    const auto sink_ok = GST_IS_APP_SINK(appsink);
    if (!src_ok || !sink_ok) {
        RCLCPP_FATAL_STREAM(
            logger,
            "Failed to generate app elements ("
            << (!src_ok ? "appsrc" : "")
            << (!src_ok && !sink_ok ? ", " : "")
            << (!sink_ok ? "appsink" : "")
            << ")"
        );

        return false;
    }

    context.source = GST_APP_SRC(appsrc);
    context.sink = GST_APP_SINK(appsink);

    g_object_set(
        G_OBJECT(context.source),
        "is-live", true,
        "format", GST_FORMAT_TIME,
        nullptr
    );

    // gst_app_sink_set_emit_signals(context.sink, true);
    // g_object_set(
    //     G_OBJECT(context.sink),
    //     "emit-signals", true,
    //     nullptr
    // );

    RCLCPP_INFO_STREAM(logger, "Initializing stream...");

    RCLCPP_INFO_STREAM(logger, "Setting stream ready...");
    if (gst_element_set_state(GST_ELEMENT(context.pipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
        const auto msg = "Could not initialise stream!";
        RCLCPP_FATAL_STREAM(logger, msg);
        return false;
    }

    return true;
}

//In a C++ world, these callbacks are (as defined above):
// StartFeedCB:     std::function<void(GstElement, guint, gstreamer_context_data)>;
// StopFeedCB:      std::function<void(GstElement, gstreamer_context_data)>;
// SampleReadyCB:   std::function<GstFlowReturn(GstElement, GstMessage , gstreamer_context_data)>;
// ErrorCB:         std::function<void(GstBus, GstMessage, gstreamer_context_data)>;
inline void configure_pipeline_callbacks(gstreamer_context_data& context) {
    g_signal_connect (context.source, "need-data", G_CALLBACK(start_feed), &context);
    g_signal_connect (context.source, "enough-data", G_CALLBACK(stop_feed), &context);
    // g_signal_connect (context.sink, "new-sample", G_CALLBACK(sample_ready), &context);

    /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
    auto bus = gst_element_get_bus(GST_ELEMENT(context.pipeline));
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(error_cb), &context);
    gst_object_unref(bus);
}

inline bool wait_for_pipeline_state_change(GstPipeline* pipeline) {
    const auto success = gst_element_get_state(GST_ELEMENT(pipeline), nullptr, nullptr, GST_CLOCK_TIME_NONE);
    return success == GST_STATE_CHANGE_SUCCESS;
}

inline GstCaps* get_caps(const sensor_msgs::msg::Image& image) {
    GstCaps* caps = nullptr;
    const auto frame_it = encoding::ros_gst_frame.find(image.encoding);
    if(frame_it != encoding::ros_gst_frame.end()) {
        caps = gst_caps_new_simple(
            "video/x-raw",
            "format", G_TYPE_STRING, frame_it->second.data(),
            "width", G_TYPE_INT, image.width,
            "height", G_TYPE_INT, image.height,
            "framerate", GST_TYPE_FRACTION, 0, 1,   //TODO: Check?
            encoding::caps_name, G_TYPE_STRING, image.header.frame_id.c_str(),
            nullptr
        );
    } else {
        const auto image_it = encoding::ros_gst_image.find(image.encoding);
        if(image_it != encoding::ros_gst_image.end()) {
            caps = gst_caps_new_simple(image_it->second.data(), nullptr, nullptr);
        }
    }

    return caps;
}

inline GstStructure* get_info(std_msgs::msg::Header header) {
    return gst_structure_new(
        encoding::info_meta_name,
        encoding::info_meta_frame_id, G_TYPE_STRING, header.frame_id.c_str(),
        encoding::info_meta_stamp, G_TYPE_INT64, rclcpp::Time(header.stamp).nanoseconds(),
        nullptr
    );
}

inline std::string frame_id_from_caps(const GstCaps* caps) {
    const auto none = "";

    if(!GST_IS_CAPS(caps)) return none;

    const auto caps_s = gst_caps_get_structure(caps, 0);
    if(!GST_IS_STRUCTURE(caps_s)) return none;

    const auto frame_id = gst_structure_get_string(GST_STRUCTURE(caps_s), encoding::caps_name);
    return frame_id ? frame_id : none;
}

inline void fill_image_details(const GstCaps* caps, sensor_msgs::msg::Image& image) {
    const auto s = gst_caps_get_structure(caps, 0);
    if(!GST_IS_STRUCTURE(s)) return;

    const auto frame_id = gst_structure_get_string(s, encoding::caps_name);
    image.header.frame_id = frame_id ? frame_id : "";
    image.is_bigendian = std::endian::native == std::endian::big;

    gint width, height;
    gboolean res;
    res = gst_structure_get_int (s, "width", &width);
    res |= gst_structure_get_int (s, "height", &height);
    if (res) {
        image.width = width;
        image.height = height;
        const auto data_size = image.data.size() / (height * width);
        image.step = data_size*width;
    }


    const auto format = gst_structure_get_string (s, "format");
    if(format) {
        bool found = false;
        const std::string_view format_str {format};
        for(const auto& it : encoding::ros_gst_frame ) {
            if (it.second == format_str) {
                image.encoding = it.first;
                found = true;
                break;
            }
        }

        if(!found) {
            for (const auto& it : encoding::ros_gst_image) {
                if (it.second == format_str) {
                    image.encoding = it.first;
                    found = true;
                    break;
                }
            }
        }

        if(!found) {
            image.encoding = format_str;
        }
    }
}


inline void send_keyframe(GstPad* pad, const GstClockTime timestamp, const GstClockTime stream_time, const GstClockTime running_time) {
    //https://github.com/centricular/gstwebrtc-demos/issues/186
    //https://gstreamer.freedesktop.org/documentation/additional/design/keyframe-force.html?gi-language=c

    // "timestamp" (G_TYPE_UINT64): the timestamp of the buffer that triggered the event.
    // "stream-time" (G_TYPE_UINT64): the stream position that triggered the event.
    // "running-time" (G_TYPE_UINT64): the running time of the stream when the event was triggered.

    const auto data = gst_structure_new(
        "GstForceKeyUnit",
        "all-headers", G_TYPE_BOOLEAN, TRUE,
        "timestamp", G_TYPE_UINT64, timestamp,
        "stream-time", G_TYPE_UINT64, stream_time,
        "running-time", G_TYPE_UINT64, running_time,
        NULL
    );
    const auto event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, data);
    gst_pad_send_event(pad, event);
}

};

};
