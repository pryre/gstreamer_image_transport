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
    virtual std::string getTransportName() const { return common::transport_name; }

protected:
  virtual void internalCallback(const typename gstreamer_image_transport::msg::DataPacket::ConstSharedPtr & message, const Callback & user_cb);

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