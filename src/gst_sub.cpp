#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/gst_sub.hpp"
#include "gst/gstcaps.h"

#include <functional>
#include <memory>
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
    _last_stamp(common::time_zero)
{
    _gst.logger = &_logger;
}

GStreamerSubscriber::~GStreamerSubscriber() {
    _gst_clean_up();
    _sub.reset();
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

            // if(common::get_pipeline_state(_gst_pipeline, 10ms) != GST_STATE_READY)
            //     RCLCPP_ERROR(_logger, "Stream is not ready!");

            // return;
    }
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
    //Correct our logger name
    _node = node;
    _logger = _node->get_logger().get_child(getModuleName());

    // Get encoder parameters
    const auto param_prefix = getModuleName() + ".";

    auto pipeline_internal_desc = rcl_interfaces::msg::ParameterDescriptor{};
    pipeline_internal_desc.description = "Encoding pipeline to use, will be prefixed by appsrc and postfixed with appsink at runtime";
    _pipeline_internal = common::trim_copy(_node->declare_parameter(param_prefix + "pipeline", _pipeline_internal, pipeline_internal_desc));

    auto force_gst_debug_desc = rcl_interfaces::msg::ParameterDescriptor{};
    force_gst_debug_desc.description = "Forces GST to output debug data messages at specified level";
    _force_debug_level = _node->declare_parameter(param_prefix + "force_gst_debug", _force_debug_level, force_gst_debug_desc);

    _image_callback = callback;
    const std::string transport_topic = common::get_topic(base_topic, getTransportName());
    auto qos = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(custom_qos), custom_qos);
    _sub = _node->create_subscription<TransportType>(
        transport_topic,
        qos,
        std::bind(&GStreamerSubscriber::_cb_packet, this, std::placeholders::_1),
        options
    );
}

void GStreamerSubscriber::reset() {
    _last_stamp = common::time_zero;

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
    common::gst_do_init(_logger);
    common::gst_set_debug_level(_force_debug_level);

    if(!common::gst_configure(_logger, _pipeline_internal, _gst)) {
        _gst_clean_up();
        const auto msg = "Unable to configure GStreamer";
        RCLCPP_FATAL_STREAM(_logger, msg);
        throw std::runtime_error(msg);
    }

    gst_app_src_set_max_buffers(_gst.source, _queue_size);
    gst_app_sink_set_max_buffers(_gst.sink, _queue_size);
    common::configure_pipeline_callbacks(_gst);

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

    if(_gst.pipeline) {
        gst_element_set_state(GST_ELEMENT(_gst.pipeline), GST_STATE_NULL);
        gst_object_unref(_gst.pipeline);
        _gst.pipeline = nullptr;

        //XXX: Pipeline will clear up all other references as well
        if(_gst.source != nullptr) _gst.source = nullptr;
        if(_gst.sink != nullptr) _gst.sink = nullptr;
    }

    if(_gst.source != nullptr) {
        gst_object_unref(_gst.source);
        _gst.source = nullptr;
    }

    if(_gst.sink != nullptr) {
        gst_object_unref(_gst.sink);
        _gst.sink = nullptr;
    }

    // RCLCPP_INFO(_logger, "Deinit GST...");
    // gst_deinit();
    // RCLCPP_INFO(_logger, "Done!");
}

void GStreamerSubscriber::_cb_packet(
  const gstreamer_image_transport::msg::DataPacket::ConstSharedPtr & message) {
    const auto frame_stamp = rclcpp::Time(message->header.stamp);
    const auto frame_delta = frame_stamp - _last_stamp;
    //TODO: Check that ROS time has been reset, and if so, reset stream

    if(frame_delta < common::duration_zero) {
        RCLCPP_WARN_STREAM(_logger, "Discarding frame due to old stamp: " << frame_delta.to_chrono<std::chrono::milliseconds>());
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
        return;
    }

    if(_last_caps.empty()) {
        RCLCPP_WARN_STREAM(_logger, "Invalid packet: no caps set");
        return;
    }

    auto caps = gst_caps_from_string(message->caps.c_str());
    if(!GST_IS_CAPS(caps) || message->caps.empty()) {
        RCLCPP_ERROR_STREAM(_logger, "Unable to parse caps: " << message->caps);

        return;
    }

    const size_t data_size = message->data.size()*sizeof(gstreamer_image_transport::msg::DataPacket::_data_type::value_type);
    auto mem = gst_memory_new_wrapped(
        GstMemoryFlags::GST_MEMORY_FLAG_READONLY, // | GstMemoryFlags::GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS
        (gpointer)message->data.data(),
        data_size, 0, data_size,
        nullptr, nullptr
    );

    GstBuffer* buffer = gst_buffer_new();
    gst_buffer_append_memory(buffer, mem);
    // const timespec ts {message->header.stamp.sec, message->header.stamp.nanosec};
    // const auto g_stamp = GST_TIMESPEC_TO_TIME(ts);
    // gst_buffer_add_reference_timestamp_meta(buffer, caps, g_stamp, GST_CLOCK_TIME_NONE);
    // auto segment_in = gst_segment_new();
    // gst_segment_init(segment_in, GstFormat::GST_FORMAT_TIME);
    // gst_segment_position_from_running_time(segment_in, GstFormat::GST_FORMAT_TIME, g_stamp);

    // auto info_in = msg->extra.empty() ? nullptr : gst_structure_from_string(msg->extra.c_str(), NULL);
    // auto info = common::get_info(message->header);

    const auto sample_in = gst_sample_new(buffer, caps, nullptr, nullptr);

    GstFlowReturn ret;
    g_signal_emit_by_name(_gst.source, "push-sample", sample_in, &ret);
    gst_sample_unref(sample_in);

    if (ret != GST_FLOW_OK) {
        RCLCPP_ERROR(_logger, "Could not push sample, frame dropped");
    }
}

bool GStreamerSubscriber::_receive_sample(GstSample* sample) {
    const auto caps = gst_sample_get_caps(sample);

    auto image = std::make_shared<sensor_msgs::msg::Image>();
    //Everything except stamp and data
    common::fill_image_details(caps, *image);

    //Get data
    GstMapInfo map;
    const auto buffer = gst_sample_get_buffer(sample);
    if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        RCLCPP_ERROR(_logger, "Cannot read sample data!");
        return false;
    }

    //Attempt to get image data, or stamp it now if it doesn't exist
    auto ref = gst_buffer_get_reference_timestamp_meta(buffer, encoding::info_reference);
    image->header.stamp = ref ? rclcpp::Time(ref->timestamp, RCL_ROS_TIME) : _node->get_clock()->now();

    const auto data = std::span(map.data, map.size);
    image->data.assign(data.begin(), data.end());
    gst_buffer_unmap (buffer, &map);

    _image_callback(image);

    return true;
}

};
