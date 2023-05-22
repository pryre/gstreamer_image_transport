#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <rclcpp/clock.hpp>
#include <rclcpp/publisher.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>
#include <map>

#include "gst/app/gstappsrc.h"
#include "gst/gstelement.h"
#include "gst/gstpipeline.h"
#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/msg/data_packet.hpp"
#include "gstreamer_image_transport/msg/detail/data_packet__struct.hpp"
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
using TransportType = gstreamer_image_transport::msg::DataPacket;

public:
    GStreamerPublisher();
    ~GStreamerPublisher();

    void reset();
    void start();
    void shutdown() override;
    void publish(const sensor_msgs::msg::Image& message) const override;

    std::string getTransportName() const override { return common::transport_name; }
    std::string getTopic() const override { return _pub ? _pub->get_topic_name() : ""; }
    size_t getNumSubscribers() const override { return _pub ? _pub->get_subscription_count() : 0; }
    std::string getModuleName() const { return common::transport_name + "_pub"; }


protected:
    void advertiseImpl(rclcpp::Node* nh, const std::string& base_topic, rmw_qos_profile_t custom_qos) override;

private:
    void _gst_clean_up();
    void _gst_run();
    void _gst_stop();

    inline std::string _get_topic(const std::string & base_topic) const { return base_topic + "/" + getTransportName(); }

private:
    std::thread _thread;
    GMainContext* _context;
    GMainLoop* _loop;

    rclcpp::Node* _node;
    rclcpp::Logger _logger;
    rclcpp::Publisher<TransportType>::SharedPtr _pub;
    bool _has_shutdown;
    std::string _pipeline_internal;
    int64_t _queue_size;
    int64_t _force_debug_level;

    // General gstreamer configuration
    std::string _encoder_hint;

    // Gstreamer structures
    GstPipeline *_gst_pipeline;
    GstAppSrc *_gst_src;
    GstAppSink *_gst_sink;

    mutable std::mutex _mutex;
    mutable rclcpp::Time _first_stamp;
    mutable rclcpp::Time _last_stamp;
};

};
