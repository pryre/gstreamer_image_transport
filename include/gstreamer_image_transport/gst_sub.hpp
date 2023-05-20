#pragma once

#include <string>

#include "image_transport/simple_subscriber_plugin.hpp"
#include "gstreamer_image_transport/msg/data_packet.hpp"

namespace gstreamer_image_transport
{

class GStreamerSubscriber : public image_transport::SimpleSubscriberPlugin
  <gstreamer_image_transport::msg::DataPacket>
{
public:
  virtual ~GStreamerSubscriber() {}

  virtual std::string getTransportName() const
  {
    return "gstreamer";
  }

protected:
  virtual void internalCallback(const typename gstreamer_image_transport::msg::DataPacket::ConstSharedPtr & message, const Callback & user_cb);
};

};