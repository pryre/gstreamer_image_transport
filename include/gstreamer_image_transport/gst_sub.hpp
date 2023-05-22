#pragma once

#include <string>

#include "gstreamer_image_transport/common.hpp"
#include "image_transport/simple_subscriber_plugin.hpp"
#include "gstreamer_image_transport/msg/data_packet.hpp"

namespace gstreamer_image_transport
{

class GStreamerSubscriber : public image_transport::SimpleSubscriberPlugin
  <gstreamer_image_transport::msg::DataPacket>
{
public:
    GStreamerSubscriber();
    ~GStreamerSubscriber();
    std::string getTransportName() const override { return common::transport_name; }
    std::string getModuleName() const { return common::transport_name + "_sub"; }

    void reset();
    void start();

protected:
  void internalCallback(const typename gstreamer_image_transport::msg::DataPacket::ConstSharedPtr & message, const Callback & user_cb) override;
  void subscribeImpl(rclcpp::Node * node, const std::string & base_topic, const Callback & callback, rmw_qos_profile_t custom_qos) override;

private:
    bool _has_shutdown;
    rclcpp::Logger _logger;
    std::string _pipeline_internal;
    int64_t _queue_size;
    int64_t _force_debug_level;
    std::string _last_caps;

    // General gstreamer configuration
    std::string _encoder_hint;

    // Gstreamer structures
    GstPipeline *_gst_pipeline;
    GstAppSrc *_gst_src;
    GstAppSink *_gst_sink;

    rclcpp::Time _last_stamp;

    void _gst_clean_up();
};

};
