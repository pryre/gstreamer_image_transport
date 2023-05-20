#include "pluginlib/class_list_macros.hpp"

#include "gstreamer_image_transport/gst_pub.hpp"
#include "gstreamer_image_transport/gst_sub.hpp"

PLUGINLIB_EXPORT_CLASS(GStreamerPublisher, image_transport::PublisherPlugin)
PLUGINLIB_EXPORT_CLASS(GStreamerSubscriber, image_transport::SubscriberPlugin)