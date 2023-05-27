#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/gst_sub.hpp"
#include "gst/gstcaps.h"

#include <functional>
#include <memory>
#include <span>
#include <rclcpp/subscription_options.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include "gst/app/gstappsink.h"
#include "gst/gstbuffer.h"
#include "gst/gstbus.h"
#include "gst/gstcaps.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstinfo.h"
#include "gst/gstmemory.h"
#include "gst/gstmessage.h"
#include "gst/gstobject.h"
#include "gst/gstpad.h"
#include "gst/gstpipeline.h"
#include "gst/gstsample.h"
#include "gst/gstsegment.h"
#include "gst/gststructure.h"

// #include "cv_bridge/cv_bridge.h"
// #include "opencv2/core/mat.hpp"
// #include "opencv2/imgproc.hpp"

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

GStreamerSubscriber::GStreamerSubscriber() :
    _logger(rclcpp::get_logger(getModuleName())),
    // _image_callback(std::bind(&GStreamerSubscriber::_image_callback_invalid, this, std::placeholders::_1)),
    _pipeline_internal("decodebin"),
    _queue_size(10),
    _force_debug_level(-1),
    _last_stamp(common::ros_time_zero),
    _dtf_min(0.0),
    _dtf_max(1000.0),
    _stats_incoming("receiving"),
    _stats_pipeline("pipeline"),
    _dtf(&_dtf_min, &_dtf_max),
    _dtt() //TODO: Tune?
{
    _gst.logger = &_logger;
}

GStreamerSubscriber::~GStreamerSubscriber() {
    _gst_clean_up();
    _sub.reset();
}

void GStreamerSubscriber::_diagnostics_configure() {
    _diagnostics = std::make_unique<diagnostic_updater::Updater>(_node);
    _diagnostics_topic_pipeline = std::make_unique<diagnostic_updater::TopicDiagnostic>(
        getTopic(), *_diagnostics, _dtf, _dtt
    );
    _diagnostics_task_pipeline = std::make_unique<diagnostic_updater::FunctionDiagnosticTask>(
        "Pipeline status", std::bind(&GStreamerSubscriber::_diagnostics_check_pipeline_stats, this, std::placeholders::_1)
    );

    _diagnostics_topic_pipeline->addTask(&*_diagnostics_task_pipeline);
}

void GStreamerSubscriber::_diagnostics_check_pipeline_stats(diagnostic_updater::DiagnosticStatusWrapper& stat) {
    const auto now = rclcpp::Time(_node->get_clock()->now(), RCL_ROS_TIME);

    _mutex_stamp.lock();
    const auto incoming_delay = _stats_incoming.delay_clean(now);
    // const auto incoming_rate = _stats_incoming.rate_clean(now);
    const auto transfer_rate = _stats_incoming.success_rate();
    const auto pipeline_delay = _stats_pipeline.delay_clean(now);
    // const auto pipeline_rate = _stats_pipeline.rate_clean(now);
    _mutex_stamp.unlock();

    if (transfer_rate > 0.9) {
        stat.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Packet loss low");
    } else if (transfer_rate > 0.5) {
        stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Packet loss medium");
    } else {
        stat.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Packet loss high");
    }

    // stat.add("Receive rate (Hz)", incoming_rate);
    stat.add("Transfer rate (%)", transfer_rate);
    stat.add("Receive delay (ms)", incoming_delay.to_chrono<std::chrono::nanoseconds>() / 1.0ms);
    // stat.add("Pipeline output rate (Hz)", pipeline_rate);
    stat.add("Pipeline delay (ms)", pipeline_delay.to_chrono<std::chrono::nanoseconds>() / 1.0ms);
}

void GStreamerSubscriber::_gst_thread_start() {
    _gst_thread_stop();

    // _gst.loop = g_main_loop_new(nullptr, false);
    // _thread = std::thread(&g_main_loop_run, _gst.loop);
    _thread = std::thread(std::bind(&GStreamerSubscriber::_gst_thread_run, this));

}

void GStreamerSubscriber::_gst_thread_run() {
    RCLCPP_INFO(_logger, "Stream receiver started.");

    bool has_preroll = false;
    while(_gst.sink && !gst_app_sink_is_eos(_gst.sink)) {
        // if(!has_preroll) {
        //     GstSample * preroll = gst_app_sink_pull_preroll(_gst.sink);
        //     if(preroll) {
        //         has_preroll = true;
        //         gst_sample_unref(preroll);
        //     }

        //     continue;
        // }

        //Pull sample back out of pipeline
        GstSample * sample = gst_app_sink_pull_sample(_gst.sink);
        if(sample) {
            if(has_preroll) {
                if(!_receive_sample(sample)) {
                    RCLCPP_ERROR(_logger, "Invalid sample received from stream.");
                }
            } else {
                RCLCPP_INFO(_logger, "Got encoder preroll");
                has_preroll = true;
            }

            gst_sample_unref(sample);
        } else {
            if(gst_app_sink_is_eos(_gst.sink)) {
                has_preroll = false;
                // RCLCPP_ERROR(_logger, "End-of-stream is in place!");
                break;
            } else {
                RCLCPP_ERROR(_logger, "Could not get gstreamer sample.");
            }
        }

        //Clean our memory queue after each sample
        //Other functions will do this at the end as well outside of this loop
        // _clean_mem_queue();
            // if(common::get_pipeline_state(_gst_pipeline, 10ms) != GST_STATE_READY)
            //     RCLCPP_ERROR(_logger, "Stream is not ready!");

            // return;
    }

    RCLCPP_INFO(_logger, "Stream receiver stopped.");
}

void GStreamerSubscriber::_gst_thread_stop() {
    if (_thread.joinable()) {
        gst_app_src_end_of_stream(_gst.source);
        _thread.join();
    }
}

void GStreamerSubscriber::shutdown() {
    reset();
    _sub.reset();
}

void GStreamerSubscriber::subscribeImpl(rclcpp::Node * node, const std::string & base_topic, const Callback& callback, rmw_qos_profile_t custom_qos, rclcpp::SubscriptionOptions options) {
    // Get encoder parameters
    _node = node->create_sub_node(getModuleName());
    //Correct our logger name
    _logger = _node->get_logger().get_child(getModuleName());

    const auto param_namespace = getModuleName() + ".";

    auto pipeline_internal_desc = rcl_interfaces::msg::ParameterDescriptor{};
    pipeline_internal_desc.description = "Encoding pipeline to use, will be prefixed by appsrc and postfixed with appsink at runtime";
    _pipeline_internal = common::trim_copy(_node->declare_parameter(param_namespace + "pipeline", _pipeline_internal, pipeline_internal_desc));

    auto force_gst_debug_desc = rcl_interfaces::msg::ParameterDescriptor{};
    force_gst_debug_desc.description = "Forces GST to output debug data messages at specified level";
    _force_debug_level = _node->declare_parameter(param_namespace + "force_gst_debug", _force_debug_level, force_gst_debug_desc);

    _image_callback = callback;
    const std::string transport_topic = common::get_topic(base_topic, getTransportName());
    auto qos = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(custom_qos), custom_qos);
    _sub = _node->create_subscription<common::TransportType>(
        transport_topic,
        qos,
        std::bind(&GStreamerSubscriber::_cb_packet, this, std::placeholders::_1),
        options
    );

    _diagnostics_configure();
    //No real way to map backwards, so map to camera/node_name
    _diagnostics->setHardwareID(_logger.get_name());
}

void GStreamerSubscriber::reset() {
    _last_stamp = common::ros_time_zero;

    //If we have a pipeline already, clean up and start over
    if(_gst.pipeline) {
        RCLCPP_INFO(_logger, "Cleaning previous pipeline...");
        gst_element_set_state(GST_ELEMENT(_gst.pipeline), GST_STATE_NULL);
        _gst_clean_up();
    }
}

void GStreamerSubscriber::start() {
    _has_shutdown = false;

    //Init GST and configure debug level as requested
    tooling::gst_do_init(_logger);
    tooling::gst_set_debug_level(_force_debug_level);

    if(!tooling::gst_configure(_pipeline_internal, _gst)) {
        _gst_clean_up();
        const auto msg = "Unable to configure GStreamer";
        RCLCPP_FATAL_STREAM(_logger, msg);
        throw std::runtime_error(msg);
    }

    gst_app_src_set_max_buffers(_gst.source, _queue_size);
    gst_app_sink_set_max_buffers(_gst.sink, _queue_size);
    tooling::configure_pipeline_callbacks(_gst);

    const auto set_ret = gst_element_set_state(GST_ELEMENT(_gst.pipeline), GST_STATE_PLAYING);
    if(set_ret == GST_STATE_CHANGE_SUCCESS) {
        RCLCPP_INFO(_logger, "Started stream!");
    } else if(set_ret == GST_STATE_CHANGE_ASYNC) {
        RCLCPP_INFO(_logger, "Waiting for stream to start...");
    } else {
        const auto msg = "Could not set pipeline to playing!";
        RCLCPP_ERROR(_logger, msg);
        throw std::runtime_error(msg);
    }

    _gst_thread_start();
}

void GStreamerSubscriber::_gst_clean_up() {
    if(_has_shutdown)
        return;

    _has_shutdown = true;

    _gst_thread_stop();

    tooling::gst_unref(_gst);
    // RCLCPP_INFO(_logger, "Deinit GST...");
    // gst_deinit();
    // RCLCPP_INFO(_logger, "Done!");
}

void GStreamerSubscriber::_cb_packet(const common::TransportType::ConstSharedPtr& message) {
    const auto now = rclcpp::Time(_node->get_clock()->now(), RCL_ROS_TIME);
    const auto frame_stamp = rclcpp::Time(message->header.stamp);
    const auto frame_delta = frame_stamp - _last_stamp;
    //TODO: Check that ROS time has been reset, and if so, reset stream

    if(frame_delta < common::duration_zero) {
        RCLCPP_WARN_STREAM(_logger, "Discarding frame due to old stamp: " << frame_delta.to_chrono<std::chrono::milliseconds>().count() << "ms");
        _stats_incoming.log(now, frame_stamp, true);
        return;
    }

    if(_last_caps != message->caps) {
        if(!_last_caps.empty()) {
            RCLCPP_WARN_STREAM(_logger, "Stream caps varied: " << message->caps);
        } else {
            RCLCPP_INFO_STREAM(_logger, "Stream caps: " << message->caps);
        }

        _last_caps = message->caps;
        //Always try to reset the pipeline if caps change
        reset();

        //Start the pipeline here as long as we don't have an empty caps message
        if(!_last_caps.empty()) {
            start();
        }
    }

    if(!_gst.feed_open) {
        RCLCPP_WARN_STREAM(_logger, "Pipeline not accepting data, packet dropped");
        _stats_incoming.log(now, frame_stamp, true);
        return;
    }

    if(_last_caps.empty()) {
        RCLCPP_WARN_STREAM(_logger, "Invalid packet: no caps set");
        _stats_incoming.log(now, frame_stamp, true);
        return;
    }

    auto caps = gst_caps_from_string(message->caps.c_str());
    if(!GST_IS_CAPS(caps) || message->caps.empty()) {
        RCLCPP_ERROR_STREAM(_logger, "Unable to parse caps: " << message->caps);
        _stats_incoming.log(now, frame_stamp, true);
        return;
    }

    _mutex_stamp.lock();
    _stats_incoming.log(now, frame_stamp);
    _mutex_stamp.unlock();

    const size_t data_size = message->data.size()*sizeof(common::TransportType::_data_type::value_type);
    auto mem = new common::MemoryContainer(message);
    auto buffer = gst_buffer_new_wrapped_full(
        GstMemoryFlags::GST_MEMORY_FLAG_READONLY,
        (gpointer)message->data.data(),
        data_size, 0, data_size,
        (gpointer)mem, common::MemoryContainer<decltype(message)>::remove
    );

    const auto g_now = common::ros_time_to_gst(now);
    const auto g_stamp = common::ros_time_to_gst(message->header.stamp);

    gst_buffer_add_reference_timestamp_meta(buffer, _gst.time_ref, g_stamp, GST_CLOCK_TIME_NONE);
    gst_buffer_add_reference_timestamp_meta(buffer, _gst.pipeline_ref, g_now, GST_CLOCK_TIME_NONE);

    const auto sample = gst_sample_new(buffer, caps, nullptr, nullptr);

    GstFlowReturn ret;
    g_signal_emit_by_name(_gst.source, "push-sample", sample, &ret);

    if (ret != GST_FLOW_OK) {
        RCLCPP_ERROR(_logger, "Could not push sample, frame dropped");
    }

    gst_sample_unref(sample);
    gst_caps_unref(caps);
    gst_buffer_unref(buffer);
}

bool GStreamerSubscriber::_receive_sample(GstSample* sample) {
    const auto now = rclcpp::Time(_node->get_clock()->now(), RCL_ROS_TIME);

    const auto caps = gst_sample_get_caps(sample);

    auto image = std::make_shared<sensor_msgs::msg::Image>();
    //Everything except stamp and data
    common::fill_image_details(caps, *image);

    const auto buffer = gst_sample_get_buffer(sample);

    //Get data
    GstMapInfo map;
    if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        RCLCPP_ERROR(_logger, "Cannot read sample data!");
        return false;
    }
    const auto data = std::span(map.data, map.size);
    image->data.assign(data.begin(), data.end());
    gst_buffer_unmap (buffer, &map);

    //Attempt to get image data, or stamp it now if it doesn't exist
    auto frame_ref = gst_buffer_get_reference_timestamp_meta(buffer, _gst.time_ref);
    image->header.stamp = frame_ref ? rclcpp::Time(frame_ref->timestamp, RCL_ROS_TIME) : _node->get_clock()->now();


    const auto ref_pipe = gst_buffer_get_reference_timestamp_meta(buffer, _gst.pipeline_ref);
    _mutex_stamp.lock();
    if(ref_pipe) {
        const auto start_pipe = rclcpp::Time(ref_pipe->timestamp, now.get_clock_type());
        _stats_pipeline.log(now, start_pipe);
    }
    _diagnostics_topic_pipeline->tick(now);
    _mutex_stamp.unlock();

    _image_callback(image);

    return true;
}

};
