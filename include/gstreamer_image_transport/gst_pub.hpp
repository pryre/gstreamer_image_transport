#pragma once

#include <string>

#include "gstreamer_image_transport/msg/data_packet.hpp"
#include "image_transport/simple_publisher_plugin.hpp"

extern "C" {
#include "gst/app/gstappsink.h"
#include "gst/gst.h"
}

class GStreamerPublisher : public image_transport::SimplePublisherPlugin<gstreamer_image_transport::msg::DataPacket> {
public:
    GStreamerPublisher();
    ~GStreamerPublisher();

    virtual std::string getTransportName() const { return "gstreamer"; }

protected:
    virtual void publish(const sensor_msgs::msg::Image &message, const PublishFn &publish_fn) const;
    virtual void advertiseImpl(rclcpp::Node * node, const std::string & base_topic, rmw_qos_profile_t custom_qos);

private:
    // General gstreamer configuration
    std::string gst_config_;

    // Gstreamer structures
    GstElement *gst_pipeline_;
    GstElement *gst_src_;
    GstElement *gst_sink_;

    // Appsink configuration
    // bool sync_sink_;
    // bool preroll_;
    // bool reopen_on_eof_;
    // bool use_gst_timestamps_;
};
