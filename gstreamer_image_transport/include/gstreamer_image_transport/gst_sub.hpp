#pragma once

#include <memory>
#include <rclcpp/logging.hpp>
#include <rclcpp/subscription_options.hpp>
#include <string>
#include <image_transport/subscriber_plugin.hpp>

#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/message_statistics.hpp"
#include "gstreamer_image_transport/tooling.hpp"
#include "gstreamer_image_transport_interfaces/msg/data_packet.hpp"
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <diagnostic_updater/publisher.hpp>

#include <deque>

namespace gstreamer_image_transport
{

class GStreamerSubscriber : public image_transport::SubscriberPlugin {

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

    void _cb_packet(const common::ConstSharedTransportType& message);
    bool _receive_sample(GstSample* sample);
    size_t _clean_mem_queue();

private:
    bool _has_shutdown;
    std::shared_ptr<rclcpp::Node> _node;
    rclcpp::Logger _logger;
    rclcpp::Subscription<common::TransportType>::SharedPtr _sub;
    Callback _image_callback;

    std::string _pipeline_internal;
    int64_t _queue_size;
    int64_t _force_debug_level;
    std::string _last_caps;

    // General gstreamer configuration
    std::string _encoder_hint;

    // Gstreamer structures
    std::thread _thread;
    tooling::gstreamer_context_data _gst;

    rclcpp::Time _last_stamp;
    std::mutex _mutex_mem;
    std::deque<common::MemoryMap<common::ConstSharedTransportType>> _mem_queue;

    mutable std::mutex _mutex_stamp;

    double _dtf_min;
    double _dtf_max;
    mutable MessageStatistics _stats_incoming;
    mutable MessageStatistics _stats_pipeline;

    diagnostic_updater::FrequencyStatusParam _dtf;
    diagnostic_updater::TimeStampStatusParam _dtt;
    std::unique_ptr<diagnostic_updater::Updater> _diagnostics;
    std::unique_ptr<diagnostic_updater::TopicDiagnostic> _diagnostics_topic_pipeline;
    std::unique_ptr<diagnostic_updater::FunctionDiagnosticTask> _diagnostics_task_pipeline;

    void _diagnostics_configure();
    void _diagnostics_check_pipeline_stats(diagnostic_updater::DiagnosticStatusWrapper& stat);
};

};
