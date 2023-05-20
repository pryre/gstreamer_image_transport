#pragma once

#include <chrono>
#include <sensor_msgs/msg/detail/image__struct.hpp>
#include <string>
#include <map>

#include "gst/app/gstappsrc.h"
#include "gst/gstelement.h"
#include "gst/gstpipeline.h"
#include "gstreamer_image_transport/msg/data_packet.hpp"
#include "image_transport/simple_publisher_plugin.hpp"
#include <sensor_msgs/image_encodings.hpp>

extern "C" {
#include "gst/app/gstappsink.h"
#include "gst/gst.h"
}

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

const std::unordered_map<std::string_view, std::string_view> ros_gst_frame_encodings {{
    {sensor_msgs::image_encodings::RGB8, "RGB"},
    {sensor_msgs::image_encodings::MONO8, "GRAY8"},
    {sensor_msgs::image_encodings::YUV422, "UYVY"},
}};

const std::unordered_map<std::string_view, std::string_view> ros_gst_image_encodings {{
    {"jpeg", "image/jpeg"},
    {"jpg", "image/jpeg"},
    {"png", "image/png"},
}};

class GStreamerPublisher : public image_transport::SimplePublisherPlugin<gstreamer_image_transport::msg::DataPacket> {
public:
    GStreamerPublisher();
    ~GStreamerPublisher();

    virtual std::string getTransportName() const { return "gstreamer"; }

protected:
    virtual void publish(const sensor_msgs::msg::Image& message, const PublishFn& publish_fn) const;
    virtual void advertiseImpl(rclcpp::Node* node, const std::string& base_topic, rmw_qos_profile_t custom_qos);

private:
    bool _has_shutdown;
    rclcpp::Logger _logger;

    // General gstreamer configuration
    std::string _gst_pipeline_str;
    std::string _encoder;

    // Gstreamer structures
    GstBin *_gst_pipeline;
    GstAppSrc *_gst_src;
    GstAppSink *_gst_sink;

    bool gst_configure();
    void gst_clean_up();
    GstCaps* get_caps(const sensor_msgs::msg::Image& image) const;
    GstState get_pipeline_state(std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero()) const;
};

};