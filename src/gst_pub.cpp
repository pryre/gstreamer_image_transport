#include "gstreamer_image_transport/gst_pub.hpp"

#include <bits/chrono.h>
#include <bits/types/struct_timespec.h>
#include <cstdint>
#include <memory>
#include <chrono>
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
    _logger(rclcpp::get_logger("gst_pub")),
    _queue_size(10),
    _force_debug_level(5),
    _gst_pipeline(nullptr),
    _gst_src(nullptr),
    _gst_sink(nullptr)
{
    //TODO: ???
}


void GStreamerPublisher::gst_clean_up() {
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

void GStreamerPublisher::advertiseImpl(rclcpp::Node * node, const std::string & base_topic, rmw_qos_profile_t custom_qos) {
    //Correct our logger name
    _logger = node->get_logger().get_child("gst_pub");

    // Get encoder parameters
    const auto param_prefix = getTransportName() + ".";

    auto pipeline_internal_desc = rcl_interfaces::msg::ParameterDescriptor{};
    pipeline_internal_desc.description = "Encoding pipeline to use, will be prefixed by appsrc and postfixed with appsink at runtime";
    const std::string pipeline_internal = common::trim_copy(node->declare_parameter(param_prefix + "pipeline", "", pipeline_internal_desc));

    auto queue_size_desc = rcl_interfaces::msg::ParameterDescriptor{};
    queue_size_desc.description = "Queue size for the input frame buffer (frames will be dropped if too many are queued)";
    _queue_size = node->declare_parameter(param_prefix + "frame_queue_size", _queue_size, queue_size_desc);

    auto force_gst_debug_desc = rcl_interfaces::msg::ParameterDescriptor{};
    force_gst_debug_desc.description = "Forces GST to output debug data messages at specified level";
    _force_debug_level = node->declare_parameter(param_prefix + "force_gst_debug", _force_debug_level, force_gst_debug_desc);

    //Get the last step in the encoder
    const auto int_split = pipeline_internal.rfind(common::pipeline_split);
    _encoder_hint = pipeline_internal.substr(int_split == std::string::npos ? 0 : int_split);
    common::trim(_encoder_hint);

    RCLCPP_INFO_STREAM(_logger, "Encoder hint: \"" << _encoder_hint << "\"");

    if(!common::gst_configure(_logger, pipeline_internal, &_gst_pipeline, &_gst_src, &_gst_sink, _queue_size, _force_debug_level)) {
        gst_clean_up();
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

    //Do implicit advertising
    SimplePublisherPlugin::advertiseImpl(node, base_topic, custom_qos);
}

GStreamerPublisher::~GStreamerPublisher() {
    gst_clean_up();
}

void GStreamerPublisher::publish(
  const sensor_msgs::msg::Image & message,
  const PublishFn & publish_fn) const
{
    if(getNumSubscribers() <= 0) {
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

    const size_t data_size = message.data.size()*sizeof(sensor_msgs::msg::Image::_data_type::value_type);
    auto mem_in = gst_memory_new_wrapped(
        GstMemoryFlags::GST_MEMORY_FLAG_READONLY, // | GstMemoryFlags::GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS
        (gpointer)message.data.data(),
        data_size, 0, data_size,
        nullptr, nullptr
    );

    GstBuffer* buffer_in = gst_buffer_new();
    gst_buffer_append_memory(buffer_in, mem_in);
    const timespec ts {message.header.stamp.sec, message.header.stamp.nanosec};
    const auto g_stamp = GST_TIMESPEC_TO_TIME(ts);

    //TODO: Figure out how to do this
    // GST_BUFFER_PTS(buffer_in) = _last_pts++;
    // GST_BUFFER_DTS(buffer_in) = _last_dts++;

    gst_buffer_add_reference_timestamp_meta(buffer_in, caps_in, g_stamp, GST_CLOCK_TIME_NONE);
    auto segment_in = gst_segment_new();
    gst_segment_init(segment_in, GstFormat::GST_FORMAT_TIME);
    gst_segment_position_from_running_time(segment_in, GstFormat::GST_FORMAT_TIME, g_stamp);

    const auto sample_in = gst_sample_new(buffer_in, caps_in, segment_in, nullptr);
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

    publish_fn(packet);

    gst_sample_unref(sample_out);
}

};