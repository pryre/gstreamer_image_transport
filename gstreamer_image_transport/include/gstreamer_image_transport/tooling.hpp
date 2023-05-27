#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
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
#include "gst/gstelement.h"
#include "gst/gstevent.h"
#include "gst/gstmeta.h"
#include "gst/gstpad.h"
#include "gst/gststructure.h"
#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"

#include <rclcpp/logger.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>

#include "gstreamer_image_transport/encoding.hpp"
#include "gstreamer_image_transport/common.hpp"

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

namespace tooling {

using ReceiveSample = std::function<bool(GstSample* sample)>;

struct gstreamer_context_data {
    // GMainContext* _context;
    // GMainLoop* loop = nullptr;
    GstPipeline *pipeline = nullptr;
    GstAppSrc *source = nullptr;
    // GstPad *source_output_pad = nullptr;
    GstAppSink *sink = nullptr;
    GstCaps *time_ref = nullptr;
    GstCaps *pipeline_ref = nullptr;
    // GstCaps *buffer_ref = nullptr;
    std::atomic<bool> feed_open {false};

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

inline std::string get_appsrc_str(const std::string_view name = common::appsrc_name) {
    return std::string("appsrc name=").append(name);//.append(configuration::pipeline_split_spaced).append(std::string("queue"));
}

inline std::string get_appsink_str(const std::string_view name = common::appsink_name) {
    // return std::string("queue").append(common::pipeline_split_spaced).append("appsink name=").append(name);
    return std::string("queue ! appsink name=").append(name);
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
inline bool gst_configure(const std::string pipeline_internal, gstreamer_context_data& context) {
    //Append our parts to the pipeline
    const auto pipeline_str = get_appsrc_str()
                            + (pipeline_internal.empty() ? "" : common::pipeline_split_spaced + pipeline_internal)
                            + common::pipeline_split_spaced
                            + get_appsink_str();
    RCLCPP_INFO_STREAM(*context.logger, "Using pipeline: \"" << pipeline_str << "\"");

    RCLCPP_INFO_STREAM(*context.logger, "Parsing pipeline...");
    GError * error = nullptr;  // Assignment to zero is a gst requirement
    const auto try_pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

    if (!GST_IS_PIPELINE(try_pipeline) || error) {
        RCLCPP_FATAL_STREAM(*context.logger, "Error parsing pipeline: " << (error ? error->message : "unknown error"));
        return false;
    }

    context.pipeline = GST_PIPELINE(try_pipeline);

    const auto appsrc = gst_bin_get_by_name(GST_BIN(context.pipeline), common::appsrc_name.c_str());
    const auto appsink = gst_bin_get_by_name(GST_BIN(context.pipeline), common::appsink_name.c_str());

    const auto src_ok = GST_IS_APP_SRC(appsrc);
    const auto sink_ok = GST_IS_APP_SINK(appsink);
    if (!src_ok || !sink_ok) {
        RCLCPP_FATAL_STREAM(
            *context.logger,
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

    // const auto pads = GST_ELEMENT_PADS(GST_ELEMENT(context.source));
    // auto p = pads->next;
    // while(p) {
    //     RCLCPP_INFO_STREAM(*context.logger, "Pad: " << GST_PAD_NAME(GST_PAD(p)));
    //     p = pads->next;
    // }

    // context.source_output_pad = gst_element_get_static_pad(GST_ELEMENT(context.source), "src");
    // if(!context.source_output_pad) {
    //     RCLCPP_ERROR(*(context.logger), "Source output pad not found, cannot generate keyframes!");
    // }

    // gst_app_sink_set_emit_signals(context.sink, true);
    // g_object_set(
    //     G_OBJECT(context.sink),
    //     "emit-signals", true,
    //     nullptr
    // );

    context.time_ref = gst_caps_from_string(encoding::info_reference);
    context.pipeline_ref = gst_caps_from_string(encoding::pipeline_reference);
    // context.buffer_ref = gst_caps_from_string(encoding::buffer_reference);

    RCLCPP_INFO_STREAM(*context.logger, "Initializing stream...");

    RCLCPP_INFO_STREAM(*context.logger, "Setting stream ready...");
    if (gst_element_set_state(GST_ELEMENT(context.pipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
        const auto msg = "Could not initialise stream!";
        RCLCPP_FATAL_STREAM(*context.logger, msg);
        return false;
    }

    return true;
}

inline void gst_unref(gstreamer_context_data& context) {
    if(context.pipeline) {
        gst_element_set_state(GST_ELEMENT(context.pipeline), GST_STATE_NULL);
        gst_object_unref(context.pipeline);
        context.pipeline = nullptr;

        //XXX: Pipeline will clear up all other references as well
        if(context.source != nullptr) context.source = nullptr;
        if(context.sink != nullptr) context.sink = nullptr;
        // if(context.source_output_pad != nullptr) context.source_output_pad = nullptr;
    }

    if(context.source != nullptr) {
        gst_object_unref(context.source);
        context.source = nullptr;
    }
    // if(context.source_output_pad != nullptr) {
    //     gst_object_unref(context.source_output_pad);
    //     context.source_output_pad = nullptr;
    // }

    if(context.sink != nullptr) {
        gst_object_unref(context.sink);
        context.sink = nullptr;
    }

    if(context.time_ref != nullptr) {
        gst_caps_unref(context.time_ref);
        context.time_ref = nullptr;
    }

    if(context.pipeline_ref != nullptr) {
        gst_caps_unref(context.pipeline_ref);
        context.pipeline_ref = nullptr;
    }

    // if(context.buffer_ref != nullptr) {
    //     gst_caps_unref(context.buffer_ref);
    //     context.buffer_ref = nullptr;
    // }
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

inline void send_keyframe(GstPipeline* pipeline, const GstClockTime timestamp, const GstClockTime stream_time, const GstClockTime running_time) {
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
    gst_element_send_event(GST_ELEMENT(pipeline), event);
    // gst_pad_send_event(pad, event);
}

};

};
