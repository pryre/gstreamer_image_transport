#pragma once

#include <chrono>
#include <cstdint>
#include <rclcpp/clock.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>
#include <map>

#include "gst/app/gstappsrc.h"
#include "gst/gstelement.h"
#include "gst/gstpipeline.h"
#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/msg/data_packet.hpp"
#include "image_transport/simple_publisher_plugin.hpp"
#include <sensor_msgs/image_encodings.hpp>
#include <string_view>

#include "gst/app/gstappsrc.h"
#include "gst/app/gstappsink.h"
#include "gst/gst.h"

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

class GStreamerPublisher : public image_transport::SimplePublisherPlugin<gstreamer_image_transport::msg::DataPacket> {
public:
    GStreamerPublisher();
    ~GStreamerPublisher();

    virtual std::string getTransportName() const { return common::transport_name; }

protected:
    virtual void publish(const sensor_msgs::msg::Image& message, const PublishFn& publish_fn) const;
    virtual void advertiseImpl(rclcpp::Node* node, const std::string& base_topic, rmw_qos_profile_t custom_qos);

private:
    bool _has_shutdown;
    rclcpp::Logger _logger;
    int64_t _queue_size;
    int64_t _force_debug_level;

    // General gstreamer configuration
    std::string _encoder_hint;

    // Gstreamer structures
    GstPipeline *_gst_pipeline;
    GstAppSrc *_gst_src;
    GstAppSink *_gst_sink;

    void gst_clean_up();
};

};