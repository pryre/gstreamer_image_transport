#include "gst/gstevent.h"
#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/gst_pub.hpp"

#include <bits/chrono.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <chrono>
#include <rcl/time.h>
#include <rcl_interfaces/msg/parameter_descriptor.hpp>
#include <rcl_interfaces/msg/parameter_type.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/node.hpp>
#include <sensor_msgs/msg/detail/image__struct.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <locale>

#include <rclcpp/executor.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sys/types.h>

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
#include "gstreamer_image_transport_interfaces/msg/data_packet.hpp"
#include "gstreamer_image_transport/tooling.hpp"
#include "rclcpp/logging.hpp"
#include "gstreamer_image_transport/common.hpp"

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

GStreamerPublisher::GStreamerPublisher() :
    _logger(rclcpp::get_logger(getModuleName())),
    _queue_size(10),
    _force_debug_level(-1),
    _first_stamp(common::ros_time_zero),
    _last_stamp(common::ros_time_zero),
    _last_key(common::ros_time_zero),
    _keyframe_interval(2s),
    _dtf_min(0.0),
    _dtf_max(1000.0),
    _stats_incoming("receiving"),
    _stats_pipeline("pipeline"),
    _dtf(&_dtf_min, &_dtf_max),
    _dtt() //TODO: Tune?
{
    _gst.logger = &_logger;
}

GStreamerPublisher::~GStreamerPublisher() {
    //TODO: Check
    // _gst_thread_stop();
    _gst_clean_up();
    _pub.reset();
}

void GStreamerPublisher::_diagnostics_configure() {
    _diagnostics = std::make_unique<diagnostic_updater::Updater>(_node);
    _diagnostics_topic_pipeline = std::make_unique<diagnostic_updater::TopicDiagnostic>(
        getTopic(), *_diagnostics, _dtf, _dtt
    );
    _diagnostics_task_pipeline = std::make_unique<diagnostic_updater::FunctionDiagnosticTask>(
        "Pipeline status", std::bind(&GStreamerPublisher::_diagnostics_check_pipeline_stats, this, std::placeholders::_1)
    );

    _diagnostics_topic_pipeline->addTask(&*_diagnostics_task_pipeline);
}

void GStreamerPublisher::_diagnostics_check_pipeline_stats(diagnostic_updater::DiagnosticStatusWrapper& stat) {
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

void GStreamerPublisher::_gst_thread_start() {
    _gst_thread_stop();

    // _gst.loop = g_main_loop_new(nullptr, false);
    _thread = std::thread(std::bind(&GStreamerPublisher::_gst_thread_run, this));
}

void GStreamerPublisher::_gst_thread_run() {
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
            // if(common::get_pipeline_state(_gst_pipeline, 10ms) != GST_STATE_READY)
            //     RCLCPP_ERROR(_logger, "Stream is not ready!");

            // return;
    }
}

void GStreamerPublisher::_gst_thread_stop() {
    if (_thread.joinable()) {
        gst_app_src_end_of_stream(_gst.source);
        _thread.join();
    }
}

void GStreamerPublisher::shutdown() {
    reset();
    _pub.reset();
}

void GStreamerPublisher::reset() {
    _first_stamp = common::ros_time_zero;
    _last_stamp = common::ros_time_zero;

    //If we have a pipeline already, clean up and start over
    if(_gst.pipeline) {
        RCLCPP_INFO(_logger, "Cleaning pipeline...");
        gst_element_set_state(GST_ELEMENT(_gst.pipeline), GST_STATE_NULL);
        _gst_clean_up();
    }
}

void GStreamerPublisher::start() {
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
    // gst_app_src_set_max_bytes(_gst.source, 10000000); //TODO
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


void GStreamerPublisher::_gst_clean_up() {
    if(_has_shutdown)
        return;

    _has_shutdown = true;

    _gst_thread_stop();

    tooling::gst_unref(_gst);

    // RCLCPP_INFO(_logger, "Deinit GST...");
    // gst_deinit();
    // RCLCPP_INFO(_logger, "Done!");
}

//TODO: Keyframes on some regular basis or when subscribers join
// if(GST_IS_ELEMENT(_gst.source)) {
//     const auto sink_pad = gst_element_get_static_pad(GST_ELEMENT(_gst.source), "sink_0");
//      send_keyframe(sink_pad, ...);
// } else {
//     RCLCPP_WARN(_logger, "Keyframe request ignored, pipeline not ready");
// }

void GStreamerPublisher::advertiseImpl(rclcpp::Node* nh, const std::string& base_topic, rmw_qos_profile_t custom_qos) {
    // Get encoder parameters
    _node = nh->create_sub_node(getModuleName());
    //Correct our logger name
    _logger = _node->get_logger().get_child(getModuleName());

    const auto param_namespace = getModuleName() + ".";

    auto pipeline_internal_desc = rcl_interfaces::msg::ParameterDescriptor{};
    pipeline_internal_desc.description = "Encoding pipeline to use, will be prefixed by appsrc and postfixed with appsink at runtime";
    _pipeline_internal = common::trim_copy(_node->declare_parameter(param_namespace + "pipeline", "", pipeline_internal_desc));

    auto queue_size_desc = rcl_interfaces::msg::ParameterDescriptor{};
    queue_size_desc.description = "Queue size for the input frame buffer (frames will be dropped if too many are queued)";
    _queue_size = _node->declare_parameter(param_namespace + "frame_queue_size", _queue_size, queue_size_desc);

    auto force_gst_debug_desc = rcl_interfaces::msg::ParameterDescriptor{};
    force_gst_debug_desc.description = "Forces GST to output debug data messages at specified level";
    _force_debug_level = _node->declare_parameter(param_namespace + "force_gst_debug", _force_debug_level, force_gst_debug_desc);

    auto keyframe_interval_desc = rcl_interfaces::msg::ParameterDescriptor{};
    keyframe_interval_desc.description = "Forces GST to output a keyframe at this interval in seconds";
    double seconds = _keyframe_interval.seconds();
    seconds = _node->declare_parameter(param_namespace + "keyframe_interval", seconds, keyframe_interval_desc);
    _keyframe_interval = rclcpp::Duration(seconds*1s);


    //Get the last step in the encoder
    const auto int_split = _pipeline_internal.rfind(common::pipeline_split);
    _encoder_hint = _pipeline_internal.substr(int_split == std::string::npos ? 0 : (int_split < (_pipeline_internal.size() - 1)) ? int_split + 1 : int_split);
    common::trim(_encoder_hint);

    RCLCPP_INFO_STREAM(_logger, "Encoder hint: \"" << _encoder_hint << "\"");

    const std::string transport_topic = common::get_topic(base_topic, getTransportName());
    const auto qos = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(custom_qos), custom_qos);
    _pub = _node->create_publisher<common::TransportType>(transport_topic, qos);

    _diagnostics_configure();
    //No real way to map backwards, so map to camera/node_name
    _diagnostics->setHardwareID(_logger.get_name());

    start();
}

void GStreamerPublisher::publishPtr(const sensor_msgs::msg::Image::ConstSharedPtr& message) const {
    const auto now = rclcpp::Time(_node->get_clock()->now(), RCL_ROS_TIME);
    const auto stamp_sync_clock = rclcpp::Time(message->header.stamp, now.get_clock_type());

    if(!_pub || (getNumSubscribers() <= 0)) {
        return;
    }

    if(!_gst.feed_open) {
        RCLCPP_WARN_STREAM(_logger, "Pipeline not accepting data, frame dropped");
        _stats_incoming.log(now, stamp_sync_clock, true);
        return;
    }

    auto caps = common::get_caps(*message);
    if(!GST_IS_CAPS(caps)) {
        RCLCPP_ERROR_STREAM(_logger,
            "Unable to prepare caps format: " << message->encoding <<
            "(" << message->width << "x" << message->height << ")"
        );

        _stats_incoming.log(now, stamp_sync_clock, true);

        return;
    }

    //XXX: At this point we should have a supported stream (assuming pipeline is happy with it)
    const auto frame_stamp = rclcpp::Time(message->header.stamp);
    const auto frame_mark = frame_stamp - common::ros_time_zero;

    //Perform all of our time calculations in lock
    _mutex_stamp.lock();
    if(_first_stamp == common::ros_time_zero) {
        _first_stamp = frame_stamp;
    }
    const auto stream_delta = frame_stamp - _first_stamp;
    const auto frame_delta = frame_stamp - _last_stamp;
    //Do check logic here to avoid mismatch errors
    const bool stream_delta_ok = stream_delta >= common::duration_zero;
    const bool frame_delta_ok = frame_delta >= common::duration_zero;
    const bool send_keyframe = frame_stamp - _last_key > _keyframe_interval;
    if(send_keyframe) _last_key = frame_stamp;
    if(stream_delta_ok && frame_delta_ok) _last_stamp = frame_stamp;

    //Log our incoming packet in the mutex as well
    _stats_incoming.log(now, stamp_sync_clock, !(stream_delta_ok && frame_delta_ok && caps));
    _mutex_stamp.unlock();

    //Handle the outcomes of the logic after unlocking
    if(!stream_delta_ok) {
        RCLCPP_WARN_STREAM(_logger, "Discarding frame, originated before stream start: " << stream_delta.to_chrono<std::chrono::milliseconds>().count() << "ms");
        //TODO: Could also do a rclcpp time check here to see if sim time has reset, and reset all from that

        gst_caps_unref(caps);
        return;
    }

    if(!frame_delta_ok) {
        RCLCPP_WARN_STREAM(_logger, "Discarding frame due to old stamp: " << frame_delta.to_chrono<std::chrono::milliseconds>().count() << "ms");

        gst_caps_unref(caps);
        return;
    }

    //XXX: At this point we should be confident that the stream is monotonic and we have a valid stamp

    const size_t data_size = message->data.size()*sizeof(sensor_msgs::msg::Image::_data_type::value_type);
    // auto mem = common::MemoryContainer<decltype(message)>::create(message);
    auto mem = new common::MemoryContainer(message);
    auto buffer = gst_buffer_new_wrapped_full(
        GstMemoryFlags::GST_MEMORY_FLAG_READONLY,
        (gpointer)message->data.data(),
        data_size, 0, data_size,
        (gpointer)mem, common::MemoryContainer<decltype(message)>::remove
    );

    //XXX: Use the stream delta to calculate our buffer timings
    const auto g_stream_stamp = common::ros_time_to_gst(stream_delta);
    const auto g_frame_stamp = common::ros_time_to_gst(frame_mark);
    const auto g_delta = common::ros_time_to_gst(frame_delta);
    const auto g_now = common::ros_time_to_gst(now);

    GST_BUFFER_PTS(buffer) = g_stream_stamp;
    GST_BUFFER_DTS(buffer) = g_stream_stamp;
    GST_BUFFER_OFFSET(buffer) = g_stream_stamp;
    GST_BUFFER_DURATION(buffer) = g_delta;

    gst_buffer_add_reference_timestamp_meta(buffer, _gst.time_ref, g_frame_stamp, GST_CLOCK_TIME_NONE);
    gst_buffer_add_reference_timestamp_meta(buffer, _gst.pipeline_ref, g_now, GST_CLOCK_TIME_NONE);

    const auto sample = gst_sample_new(buffer, caps, nullptr, nullptr);

    GstFlowReturn ret = GST_FLOW_ERROR;
    g_signal_emit_by_name(_gst.source, "push-sample", sample, &ret);
    gst_sample_unref(sample);
    gst_caps_unref(caps);

    if (ret != GST_FLOW_OK) {
        RCLCPP_ERROR(_logger, "Could not push sample, frame dropped");
    }

    gst_buffer_unref(buffer);

    if(send_keyframe && _gst.pipeline) {
        tooling::send_keyframe(_gst.pipeline, g_stream_stamp, g_stream_stamp, g_stream_stamp);
    }
}

bool GStreamerPublisher::_receive_sample(GstSample* sample) {
    const auto now = rclcpp::Time(_node->get_clock()->now(), RCL_ROS_TIME);

    common::TransportType packet;
    //We stamp the packet with the current time, and we'll extract the image time from the encoded data
    //There is no guarantee that this packet is one image, a piece of an image, or something else
    packet.header.stamp = now;

    //Get packet data
    const auto caps = gst_sample_get_caps(sample);
    packet.header.frame_id = common::frame_id_from_caps(caps);  //TODO: Work this out
    packet.caps = caps ? gst_caps_to_string(caps) : "";
    packet.encoder = _encoder_hint;

    //Get data
    GstMapInfo map;
    const auto buffer = gst_sample_get_buffer(sample);
    if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        RCLCPP_ERROR(_logger, "Cannot read sample data!");
        return false;
    }
    const auto data = std::span(map.data, map.size);
    packet.data.assign(data.begin(), data.end());
    gst_buffer_unmap (buffer, &map);

    const auto ref = gst_buffer_get_reference_timestamp_meta(buffer, _gst.time_ref);
    packet.frame_stamp = ref ? rclcpp::Time(ref->timestamp, RCL_ROS_TIME) : common::ros_time_zero;

    const auto ref_pipe = gst_buffer_get_reference_timestamp_meta(buffer, _gst.pipeline_ref);
    if(ref_pipe) {
        const auto start_pipe = rclcpp::Time(ref_pipe->timestamp, now.get_clock_type());
        _mutex_stamp.lock();
        _stats_pipeline.log(now, start_pipe);
        _mutex_stamp.unlock();
    }

    _diagnostics_topic_pipeline->tick(now);

    _pub->publish(packet);

    return true;
}
};
