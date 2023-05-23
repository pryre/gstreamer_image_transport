#pragma once

#include <rclcpp/logging.hpp>
#include <rclcpp/subscription_options.hpp>
#include <string>

#include "gstreamer_image_transport/common.hpp"
#include "image_transport/simple_subscriber_plugin.hpp"
#include "gstreamer_image_transport/msg/data_packet.hpp"

namespace gstreamer_image_transport
{

class GStreamerSubscriber : public image_transport::SubscriberPlugin {

using TransportType = gstreamer_image_transport::msg::DataPacket;

public:
    GStreamerSubscriber();
    ~GStreamerSubscriber();
    std::string getTransportName() const override { return common::transport_name; }
    std::string getModuleName() const { return common::transport_name + "_sub"; }
    std::string getTopic() const override { return _sub ? _sub->get_topic_name() : ""; }
    size_t getNumPublishers() const override { return _sub ? _sub->get_publisher_count() : 0; }

    void reset();
    void start();
    void shutdown() override;

protected:
    void subscribeImpl(rclcpp::Node* node, const std::string& base_topic, const Callback& callback, rmw_qos_profile_t custom_qos, rclcpp::SubscriptionOptions options) override;
    inline void subscribeImpl(rclcpp::Node* node, const std::string& base_topic, const Callback& callback, rmw_qos_profile_t custom_qos) override {
        subscribeImpl(node, base_topic, callback, custom_qos, rclcpp::SubscriptionOptions());
    };

private:
    // inline void _image_callback_invalid(const sensor_msgs::msg::Image::ConstSharedPtr image) { RCLCPP_WARN_STREAM(_logger, "Invalid image callback, image from " << image->header.frame_id << "dropped"); };

    void _gst_clean_up();
    void _gst_thread_start();
    void _gst_thread_run();
    void _gst_thread_stop();

    void _cb_packet(const typename gstreamer_image_transport::msg::DataPacket::ConstSharedPtr& message);
    bool _receive_sample(GstSample* sample);

private:
    bool _has_shutdown;
    rclcpp::Node* _node;
    rclcpp::Logger _logger;
    rclcpp::Subscription<TransportType>::SharedPtr _sub;
    Callback _image_callback;

    std::string _pipeline_internal;
    int64_t _queue_size;
    int64_t _force_debug_level;
    std::string _last_caps;

    // General gstreamer configuration
    std::string _encoder_hint;

    // Gstreamer structures
    std::thread _thread;
    gstreamer_context_data _gst;

    rclcpp::Time _last_stamp;
};

};
