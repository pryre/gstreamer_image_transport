#include "gst/gstevent.h"
#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/gst_pub.hpp"

#include <bits/chrono.h>
#include <bits/types/struct_timespec.h>
#include <cstdint>
#include <memory>
#include <chrono>
#include <rcl/time.h>
#include <rcl_interfaces/msg/parameter_descriptor.hpp>
#include <rcl_interfaces/msg/parameter_type.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/node.hpp>
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
#include "gstreamer_image_transport/msg/data_packet.hpp"
#include "rclcpp/logging.hpp"
#include "gstreamer_image_transport/common.hpp"

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

GStreamerPublisher::GStreamerPublisher() :
    _logger(rclcpp::get_logger(getModuleName())),
    _queue_size(10),
    _force_debug_level(0),
    _gst_pipeline(nullptr),
    _gst_src(nullptr),
    _gst_sink(nullptr),
    _first_stamp(common::time_zero),
    _last_stamp(common::time_zero)
{
    //TODO: Check
    _context = g_main_context_default();
    _thread = std::thread(&GStreamerPublisher::_gst_run(), this);
}


void GStreamerPublisher::_gst_run() {
    //TODO: Check
    _loop = g_main_loop_new(_context, true);
    g_main_loop_run(_loop);
}

void GStreamerPublisher::_gst_stop() {
  if (_thread.joinable()) {
    //TODO: Check
    g_main_loop_quit(_loop);
    _thread.join();
  }
}

GStreamerPublisher::~GStreamerPublisher() {
    //TODO: Check
    _gst_clean_up();
    _gst_stop();
}

void GStreamerPublisher::shutdown() {
    reset();
    _pub.reset();
}


void GStreamerPublisher::reset() {
    _first_stamp = common::time_zero;
    _last_stamp = common::time_zero;

    //If we have a pipeline already, clean up and start over
    if(_gst_pipeline) {
        RCLCPP_INFO(_logger, "Cleaning previous pipeline...");
        gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_NULL);
        _gst_clean_up();
    }
}

void GStreamerPublisher::start() {
    if(!common::gst_configure(_logger, _pipeline_internal, &_gst_pipeline, &_gst_src, &_gst_sink, _queue_size, _force_debug_level)) {
        _gst_clean_up();
        const auto msg = "Unable to configure GStreamer";
        RCLCPP_FATAL_STREAM(_logger, msg);
        throw std::runtime_error(msg);
    }

    const auto set_ret = gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_PLAYING);
    if(set_ret == GST_STATE_CHANGE_ASYNC) {
        RCLCPP_WARN(_logger, "Waiting for system stream to ready...");
    } else if(set_ret != GST_STATE_CHANGE_SUCCESS) {
        const auto msg = "Could not set pipeline to playing!";
        RCLCPP_ERROR(_logger, msg);
        throw std::runtime_error(msg);
    } else {
        RCLCPP_INFO(_logger, "Started stream!");
    }
}


void GStreamerPublisher::_gst_clean_up() {
    if(_has_shutdown)
        return;

    // RCLCPP_INFO(_logger, "Shutting down...");

    if(_gst_pipeline) {
        gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_NULL);
        gst_object_unref(_gst_pipeline);
        _gst_pipeline = nullptr;

        //XXX: Pipeline will clear up all other references as well
        if(_gst_src != nullptr) _gst_src = nullptr;
        if(_gst_sink != nullptr) _gst_sink = nullptr;
    }

    if(_gst_src != nullptr) {
        gst_object_unref(_gst_src);
        _gst_src = nullptr;
    }

    if(_gst_sink != nullptr) {
        gst_object_unref(_gst_sink);
        _gst_sink = nullptr;
    }

    // RCLCPP_INFO(_logger, "Deinit GST...");
    // gst_deinit();
    // RCLCPP_INFO(_logger, "Done!");

    _has_shutdown = true;
}

//TODO: Keyframes on some regular basis or when subscribers join
// if(GST_IS_ELEMENT(_gst_src)) {
//     const auto sink_pad = gst_element_get_static_pad(GST_ELEMENT(_gst_src), "sink_0");
//      send_keyframe(sink_pad, ...);
// } else {
//     RCLCPP_WARN(_logger, "Keyframe request ignored, pipeline not ready");
// }

void GStreamerPublisher::advertiseImpl(rclcpp::Node* nh, const std::string& base_topic, rmw_qos_profile_t custom_qos) {
    //Correct our logger name
    _node = nh;
    _logger = _node->get_logger().get_child(getModuleName());

    // Get encoder parameters
    const auto param_prefix = getModuleName() + ".";

    auto pipeline_internal_desc = rcl_interfaces::msg::ParameterDescriptor{};
    pipeline_internal_desc.description = "Encoding pipeline to use, will be prefixed by appsrc and postfixed with appsink at runtime";
    _pipeline_internal = common::trim_copy(_node->declare_parameter(param_prefix + "pipeline", "", pipeline_internal_desc));

    auto queue_size_desc = rcl_interfaces::msg::ParameterDescriptor{};
    queue_size_desc.description = "Queue size for the input frame buffer (frames will be dropped if too many are queued)";
    _queue_size = _node->declare_parameter(param_prefix + "frame_queue_size", _queue_size, queue_size_desc);

    auto force_gst_debug_desc = rcl_interfaces::msg::ParameterDescriptor{};
    force_gst_debug_desc.description = "Forces GST to output debug data messages at specified level";
    _force_debug_level = _node->declare_parameter(param_prefix + "force_gst_debug", _force_debug_level, force_gst_debug_desc);

    //Get the last step in the encoder
    const auto int_split = _pipeline_internal.rfind(common::pipeline_split);
    _encoder_hint = _pipeline_internal.substr(int_split == std::string::npos ? 0 : (int_split < (_pipeline_internal.size() - 1)) ? int_split + 1 : int_split);
    common::trim(_encoder_hint);

    RCLCPP_INFO_STREAM(_logger, "Encoder hint: \"" << _encoder_hint << "\"");

    const std::string transport_topic = _get_topic(base_topic);
    const auto qos = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(custom_qos), custom_qos);
    _pub = _node->create_publisher<TransportType>(transport_topic, qos);

    start();
}

void GStreamerPublisher::publish(const sensor_msgs::msg::Image & message) const {
    if(!_pub || (getNumSubscribers() <= 0)) {
        return;
    }

    auto caps_in = common::get_caps(message);
    if(!GST_IS_CAPS(caps_in)) {
        RCLCPP_ERROR_STREAM(_logger,
            "Unable to reconfigure to format: " << message.encoding <<
            "(" << message.width << "x" << message.height << ")"
        );

        return;
    }

    //XXX: At this point we should have a supported stream (assuming pipeline is happy with it)
    const auto frame_stamp = rclcpp::Time(message.header.stamp);

    //Perform all of our time calculations in lock
    _mutex.lock();
    if(_first_stamp == common::time_zero) {
        _first_stamp = frame_stamp;
    }
    const auto stream_delta = frame_stamp - _first_stamp;
    const auto frame_delta = frame_stamp - _last_stamp;
    //Do check logic here to avoid mismatch errors
    const bool stream_delta_ok = stream_delta >= common::duration_zero;
    const bool frame_delta_ok = frame_delta >= common::duration_zero;
    if(stream_delta_ok && frame_delta_ok) _last_stamp = frame_stamp;
    _mutex.unlock();

    //Handle the outcomes of the logic after unlocking
    if(!stream_delta_ok) {
        RCLCPP_WARN_STREAM(_logger, "Discarding frame, originated before stream start: " << stream_delta.to_chrono<std::chrono::milliseconds>());

        //TODO: Could also do a rclcpp time check here to see if sim time has reset, and reset all from that
        return;
    }

    if(!frame_delta_ok) {
        RCLCPP_WARN_STREAM(_logger, "Discarding frame due to old stamp: " << frame_delta.to_chrono<std::chrono::milliseconds>());
        return;
    }


    // RCLCPP_INFO_STREAM(_logger, "Frame delta: " << frame_delta.to_chrono<std::chrono::milliseconds>());
    //XXX: At this point we should be confident that the stream is monotonic and we have a valid stamp

    const size_t data_size = message.data.size()*sizeof(sensor_msgs::msg::Image::_data_type::value_type);
    auto mem_in = gst_memory_new_wrapped(
        GstMemoryFlags::GST_MEMORY_FLAG_READONLY, // | GstMemoryFlags::GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS
        (gpointer)message.data.data(),
        data_size, 0, data_size,
        nullptr, nullptr
    );

    // RCLCPP_INFO(_logger, "BUFFER");
    GstBuffer* buffer_in = gst_buffer_new();
    gst_buffer_append_memory(buffer_in, mem_in);
    //XXX: Use the stream delta to calculate our buffer timings
    const auto g_stamp = common::ros_time_to_gst(stream_delta);
    const auto g_delta = common::ros_time_to_gst(frame_delta);

    // RCLCPP_INFO(_logger, "BUFFER_TIME");
    GST_BUFFER_PTS(buffer_in) = g_stamp;
    GST_BUFFER_DTS(buffer_in) = g_stamp;
    GST_BUFFER_OFFSET(buffer_in) = g_stamp;
    GST_BUFFER_DURATION(buffer_in) = g_delta;

    // gst_buffer_add_reference_timestamp_meta(buffer_in, caps_in, g_stamp, GST_CLOCK_TIME_NONE);
    // auto segment_in = gst_segment_new();
    // gst_segment_init(segment_in, GstFormat::GST_FORMAT_TIME);
    // gst_segment_position_from_running_time(segment_in, GstFormat::GST_FORMAT_TIME, g_stamp);

    // RCLCPP_INFO(_logger, "SAMPLE");
    const auto sample_in = gst_sample_new(buffer_in, caps_in, nullptr, nullptr);

    // RCLCPP_INFO(_logger, "PUSH");
    const auto push_result = gst_app_src_push_sample(_gst_src, sample_in);
    //XXX: Sample owns buffer, no need to unref
    gst_sample_unref(sample_in);

    if(push_result != GST_FLOW_OK) {
        RCLCPP_ERROR(_logger, "Could not push sample, frame dropped");
        return;
    }

    //Pull sample back out of pipeline
    GstSample * sample_out = gst_app_sink_try_pull_sample(_gst_sink, GST_SECOND);
    if (!sample_out) {
        RCLCPP_ERROR(_logger, "Could not get gstreamer sample.");
        if(gst_app_sink_is_eos(_gst_sink))
            RCLCPP_ERROR(_logger, "End-of-stream is in place!");

        if(common::get_pipeline_state(_gst_pipeline, 10ms) != GST_STATE_READY)
            RCLCPP_ERROR(_logger, "Stream is not ready!");

        return;
    }

    const auto buffer_out = gst_sample_get_buffer(sample_out);
    const auto memory_out = gst_buffer_get_memory(buffer_out, 0);
    GstMapInfo info;
    gst_memory_map(memory_out, &info, GST_MAP_READ);
    const auto data_out = std::span(info.data, info.size);
    const auto caps_out = gst_sample_get_caps(sample_out);
    const auto info_out = gst_sample_get_info(sample_out);

    gstreamer_image_transport::msg::DataPacket packet;
    packet.header = message.header;
    packet.encoder = _encoder_hint;
    packet.caps = caps_out ? gst_caps_to_string(caps_out) : "";
    packet.extra = info_out ? gst_structure_to_string(info_out) : "";
    packet.data.assign(data_out.begin(), data_out.end());

    _pub->publish(packet);

    gst_sample_unref(sample_out);
}

};
