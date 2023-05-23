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
#include "gst/gstmeta.h"
#include "gst/gstpad.h"
#include "gst/gststructure.h"
#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"

#include <rclcpp/logger.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>

#include "gstreamer_image_transport/encoding.hpp"

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

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

};

};
