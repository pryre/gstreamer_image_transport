#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <rclcpp/clock.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/publisher.hpp>
#include <sensor_msgs/msg/detail/image__struct.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>
#include <deque>

#include "gst/app/gstappsrc.h"
#include "gst/gstelement.h"
#include "gst/gstpipeline.h"
#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/tooling.hpp"
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

//TODO: Inherit straight from publisher plugin and run a thread for gstreamer
class GStreamerPublisher : public image_transport::PublisherPlugin {

public:
    GStreamerPublisher();
    ~GStreamerPublisher();

    void reset();
    void start();
    void shutdown() override;

    void publish(const sensor_msgs::msg::Image& message) const override {
        RCLCPP_WARN_ONCE(_logger, "Camera driver is giving image copies!");
        const auto image = std::make_shared<const sensor_msgs::msg::Image>(message);
        publishPtr(image);
    }
    void publishPtr(const sensor_msgs::msg::Image::ConstSharedPtr & message) const  override;
    inline void publishData(const sensor_msgs::msg::Image & message, const uint8_t * data) const  override
    {
        sensor_msgs::msg::Image msg;
        msg.header = message.header;
        msg.height = message.height;
        msg.width = message.width;
        msg.encoding = message.encoding;
        msg.is_bigendian = message.is_bigendian;
        msg.step = message.step;
        msg.data = std::vector<uint8_t>(data, data + msg.step * msg.height);

        publish(msg);
    }

    std::string getTransportName() const override { return common::transport_name; }
    std::string getTopic() const override { return _pub ? _pub->get_topic_name() : ""; }
    size_t getNumSubscribers() const override { return _pub ? _pub->get_subscription_count() : 0; }
    std::string getModuleName() const { return common::transport_name + "_pub"; }


protected:
    void advertiseImpl(rclcpp::Node* nh, const std::string& base_topic, rmw_qos_profile_t custom_qos) override;

private:
    void _gst_clean_up();
    size_t _clean_mem_queue();
    void _gst_thread_start();
    void _gst_thread_run();
    void _gst_thread_stop();

    bool _receive_sample(GstSample* sample);

private:
    rclcpp::Node* _node;
    rclcpp::Logger _logger;
    rclcpp::Publisher<common::TransportType>::SharedPtr _pub;
    bool _has_shutdown;
    std::string _pipeline_internal;
    int64_t _queue_size;
    int64_t _force_debug_level;

    // General gstreamer configuration
    std::string _encoder_hint;

    // Gstreamer structures
    std::thread _thread;
    tooling::gstreamer_context_data _gst;

    mutable std::mutex _mutex_stamp;
    mutable rclcpp::Time _first_stamp;
    mutable rclcpp::Time _last_stamp;
    mutable rclcpp::Time _last_key;
    mutable rclcpp::Duration _keyframe_interval;

    mutable std::mutex _mutex_mem;
    mutable std::deque<common::MemoryMap<common::ConstSharedImageType>> _mem_queue;
};

};
