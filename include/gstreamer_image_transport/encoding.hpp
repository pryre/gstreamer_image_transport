#pragma once

#include <gst/gstcaps.h>
#include <unordered_map>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>

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

const auto caps_name = "name";

const auto info_meta_name = "ros-frame";
const auto info_meta_frame_id = "id";
const auto info_meta_stamp = "stamp";

const auto info_reference = gst_caps_from_string("timestamp/x-ros-camera-stream");

};

};
