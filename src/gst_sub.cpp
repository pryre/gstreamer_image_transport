#include "gstreamer_image_transport/gst_sub.hpp"
#include "gst/gstcaps.h"

#include <memory>
#include <sensor_msgs/msg/detail/image__struct.hpp>

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
    _logger(rclcpp::get_logger("gst_sub")),
    _queue_size(10),
    _force_debug_level(5),
    _gst_pipeline(nullptr),
    _gst_src(nullptr),
    _gst_sink(nullptr)
{
    // // Get encoder parameters
    // const auto param_prefix = getTransportName() + ".";

    // auto pipeline_internal_desc = rcl_interfaces::msg::ParameterDescriptor{};
    // pipeline_internal_desc.description = "Encoding pipeline to use, will be prefixed by appsrc and postfixed with appsink at runtime";
    // const std::string pipeline_internal = common::trim_copy(node->declare_parameter(param_prefix + "pipeline", "", pipeline_internal_desc));

    // auto queue_size_desc = rcl_interfaces::msg::ParameterDescriptor{};
    // queue_size_desc.description = "Queue size for the input frame buffer (frames will be dropped if too many are queued)";
    // _queue_size = node->declare_parameter(param_prefix + "frame_queue_size", _queue_size, queue_size_desc);

    // auto force_gst_debug_desc = rcl_interfaces::msg::ParameterDescriptor{};
    // force_gst_debug_desc.description = "Forces GST to output debug data messages at specified level";
    // _force_debug_level = node->declare_parameter(param_prefix + "force_gst_debug", _force_debug_level, force_gst_debug_desc);

    //Get the last step in the encoder
    reset();
}

GStreamerSubscriber::~GStreamerSubscriber() {
    gst_clean_up();
}

void GStreamerSubscriber::reset() {
    //If we have a pipeline already, clean up and start over
    if(_gst_pipeline) {
        RCLCPP_INFO(_logger, "Cleaning previous pipeline...");
        gst_element_set_state(GST_ELEMENT(_gst_pipeline), GST_STATE_NULL);
        gst_clean_up();
    }

    if(!common::gst_configure(_logger, "decodebin", &_gst_pipeline, &_gst_src, &_gst_sink, _queue_size, _force_debug_level)) {
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
}

void GStreamerSubscriber::internalCallback(
  const gstreamer_image_transport::msg::DataPacket::ConstSharedPtr & msg,
  const Callback & user_cb) {

    if(_last_caps != msg->caps) {
        if(!_last_caps.empty()) {
            RCLCPP_WARN_STREAM(_logger, "Message caps varied: " << msg->caps);
            reset();
        } else {
            RCLCPP_INFO_STREAM(_logger, "Stream started: " << msg->caps);
        }

        _last_caps = msg->caps;
    }

    auto caps_in = gst_caps_from_string(msg->caps.c_str());
    if(!GST_IS_CAPS(caps_in) || msg->caps.empty()) {
        RCLCPP_ERROR_STREAM(_logger, "Unable to parse caps: " << msg->caps);

        return;
    }

    const size_t data_size = msg->data.size()*sizeof(gstreamer_image_transport::msg::DataPacket::_data_type::value_type);
    auto mem_in = gst_memory_new_wrapped(
        GstMemoryFlags::GST_MEMORY_FLAG_READONLY, // | GstMemoryFlags::GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS
        (gpointer)msg->data.data(),
        data_size, 0, data_size,
        nullptr, nullptr
    );

    GstBuffer* buffer_in = gst_buffer_new();
    gst_buffer_append_memory(buffer_in, mem_in);
    const timespec ts {msg->header.stamp.sec, msg->header.stamp.nanosec};
    const auto g_stamp = GST_TIMESPEC_TO_TIME(ts);
    gst_buffer_add_reference_timestamp_meta(buffer_in, caps_in, g_stamp, GST_CLOCK_TIME_NONE);
    // auto segment_in = gst_segment_new();
    // gst_segment_init(segment_in, GstFormat::GST_FORMAT_TIME);
    // gst_segment_position_from_running_time(segment_in, GstFormat::GST_FORMAT_TIME, g_stamp);

    auto info_in = msg->extra.empty() ? nullptr : gst_structure_from_string(msg->extra.c_str(), NULL);

    const auto sample_in = gst_sample_new(buffer_in, caps_in, nullptr, info_in);
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

        if(common::get_pipeline_state(_gst_pipeline, 1s) != GST_STATE_READY)
            RCLCPP_ERROR(_logger, "Stream is not ready!");

        return;
    }

    const auto buffer_out = gst_sample_get_buffer(sample_out);
    const auto memory_out = gst_buffer_get_memory(buffer_out, 0);
    GstMapInfo info;
    gst_memory_map(memory_out, &info, GST_MAP_READ);
    const auto data_out = std::span(info.data, info.size);
    const auto caps_out = gst_sample_get_caps(sample_out);

    auto image = std::make_shared<sensor_msgs::msg::Image>();
    image->header = msg->header;
    image->data.assign(data_out.begin(), data_out.end());
    common::fill_image_details(caps_out, *image);

    gst_sample_unref(sample_out);

    user_cb(image);
}

void GStreamerSubscriber::gst_clean_up() {
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

};
