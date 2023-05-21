#pragma once

#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"
#include <string>
#include <algorithm>
#include <map>
#include <unordered_map>

#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <chrono>

#include <gst/gstpipeline.h>
#include "gst/gstcapsfeatures.h"
#include "gst/gststructure.h"
#include "gstreamer_image_transport/common.hpp"
#include <memory>
#include <rclcpp/logger.hpp>
#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <string_view>


using namespace std::chrono_literals;

namespace gstreamer_image_transport
{


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

};

namespace common {

constexpr std::string appsrc_name = "source";
constexpr std::string appsink_name = "sink";
constexpr std::string pipeline_split = "!";
constexpr std::string pipeline_split_spaced = " ! ";
constexpr std::string transport_name = "gst";


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
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - std::chrono::seconds(seconds)).count();
        timespec ts {seconds, nanoseconds};
        gt = GST_TIMESPEC_TO_TIME(ts);
    }

    // const auto success = gst_element_get_state(GST_ELEMENT(_gst_pipeline), &current_state, &pending_state, gt);
    gst_element_get_state(GST_ELEMENT(pipeline), &current_state, nullptr, gt);
    // if(success == GST_STATE_CHANGE_) {
    //     RCLCPP_FATAL_STREAM(_logger, "Last stream state change failed (" << success << ")!");
    // }

    // return success == GST_STATE_CHANGE_SUCCESS ? current_state : pending_state;
    return current_state;
}

inline bool gst_configure(const rclcpp::Logger logger, const std::string pipeline_internal, GstPipeline** pipeline, GstAppSrc** source, GstAppSink** sink, const int64_t src_queue_size, const int64_t debug_level = 0){
    RCLCPP_INFO_STREAM(logger, "Initializing gstreamer...");
    gst_init(nullptr, nullptr);
    RCLCPP_INFO_STREAM(logger, "GStreamer Version: " << gst_version_string() );

    if (!gst_debug_is_active() && debug_level) {
        gst_debug_set_active(TRUE);
        GstDebugLevel level = gst_debug_get_default_threshold();
        if (level < GST_LEVEL_ERROR) {
            level = GST_LEVEL_ERROR;
            gst_debug_set_default_threshold(level);
        }
    }

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

    *pipeline = GST_PIPELINE(try_pipeline);

    const auto appsrc = gst_bin_get_by_name(GST_BIN(*pipeline), appsrc_name.c_str());
    const auto appsink = gst_bin_get_by_name(GST_BIN(*pipeline), appsink_name.c_str());

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

    *source = GST_APP_SRC(appsrc);
    *sink = GST_APP_SINK(appsink);

    g_object_set(
        G_OBJECT(*source),
        "is-live", true,
        "format", GST_FORMAT_TIME,
        nullptr
    );

    gst_app_src_set_max_buffers(*source, src_queue_size);
    // gst_app_src_set_max_bytes(_gst_src, 10000000); //TODO

    RCLCPP_INFO_STREAM(logger, "Initializing stream...");

    if(get_pipeline_state(*pipeline, 1s) == GST_STATE_NULL) {
        RCLCPP_INFO_STREAM(logger, "Setting stream ready...");
        if (gst_element_set_state(GST_ELEMENT(*pipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
            const auto msg = "Could not initialise stream!";
            RCLCPP_FATAL_STREAM(logger, msg);
            return false;
        }
    }

    return get_pipeline_state(*pipeline, 5s) == GST_STATE_READY;
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

inline void fill_image_details(const GstCaps* caps, sensor_msgs::msg::Image& image) {
    const auto s = gst_caps_get_structure(caps, 0);

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

};

};