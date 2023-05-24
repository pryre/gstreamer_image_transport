#include "gst/gstevent.h"
#include "gstreamer_image_transport/common.hpp"
#include "gstreamer_image_transport/gst_pub.hpp"

#include <bits/chrono.h>
#include <bits/types/struct_timespec.h>
#include <cstdint>
#include <cstring>
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
    _keyframe_interval(2s)
{
    _gst.logger = &_logger;
}

GStreamerPublisher::~GStreamerPublisher() {
    //TODO: Check
    // _gst_thread_stop();
    _gst_clean_up();
    _pub.reset();
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

    if(!tooling::gst_configure(_logger, _pipeline_internal, _gst)) {
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

    auto keyframe_interval_desc = rcl_interfaces::msg::ParameterDescriptor{};
    keyframe_interval_desc.description = "Forces GST to output a keyframe at this interval in seconds";
    double seconds = _keyframe_interval.seconds();
    seconds = _node->declare_parameter(param_prefix + "keyframe_interval", seconds, keyframe_interval_desc);
    _keyframe_interval = rclcpp::Duration(seconds*1s);


    //Get the last step in the encoder
    const auto int_split = _pipeline_internal.rfind(common::pipeline_split);
    _encoder_hint = _pipeline_internal.substr(int_split == std::string::npos ? 0 : (int_split < (_pipeline_internal.size() - 1)) ? int_split + 1 : int_split);
    common::trim(_encoder_hint);

    RCLCPP_INFO_STREAM(_logger, "Encoder hint: \"" << _encoder_hint << "\"");

    const std::string transport_topic = common::get_topic(base_topic, getTransportName());
    const auto qos = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(custom_qos), custom_qos);
    _pub = _node->create_publisher<TransportType>(transport_topic, qos);

    start();
}

void GStreamerPublisher::publish(const sensor_msgs::msg::Image & message) const {
    if(!_pub || (getNumSubscribers() <= 0)) {
        return;
    }

    if(!_gst.feed_open) {
        RCLCPP_WARN_STREAM(_logger, "Pipeline not accepting data, frame dropped");
        return;
    }

    auto caps = common::get_caps(message);
    if(!GST_IS_CAPS(caps)) {
        RCLCPP_ERROR_STREAM(_logger,
            "Unable to reconfigure to format: " << message.encoding <<
            "(" << message.width << "x" << message.height << ")"
        );

        return;
    }

    //XXX: At this point we should have a supported stream (assuming pipeline is happy with it)
    const auto frame_stamp = rclcpp::Time(message.header.stamp);
    const auto frame_mark = frame_stamp - common::ros_time_zero;

    //Perform all of our time calculations in lock
    _mutex.lock();
    if(_first_stamp == common::ros_time_zero) {
        _first_stamp = frame_stamp;
    }
    const auto stream_delta = frame_stamp - _first_stamp;
    const auto frame_delta = frame_stamp - _last_stamp;
    //Do check logic here to avoid mismatch errors
    const bool stream_delta_ok = stream_delta >= common::duration_zero;
    const bool frame_delta_ok = frame_delta >= common::duration_zero;
    const bool send_keyframe = _last_key - frame_stamp > _keyframe_interval;
    if(send_keyframe) _last_key = frame_stamp;
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

    //XXX: At this point we should be confident that the stream is monotonic and we have a valid stamp

    const size_t data_size = message.data.size()*sizeof(sensor_msgs::msg::Image::_data_type::value_type);
    GstBuffer* buffer = gst_buffer_new_and_alloc(data_size);
    //XXX: Non-copy insert
    // auto mem = gst_memory_new_wrapped(
    //     GstMemoryFlags::GST_MEMORY_FLAG_READONLY, // | GstMemoryFlags::GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS
    //     (gpointer)message.data.data(),
    //     data_size, 0, data_size,
    //     nullptr, nullptr
    // );
    // gst_buffer_append_memory(buffer, mem);

    GstMapInfo map;
    if(!gst_buffer_map (buffer, &map, GST_MAP_WRITE)) {
        RCLCPP_ERROR(_logger, "Could allocate buffer");
        gst_buffer_unref(buffer);
        return;
    }
    const auto data = std::span(map.data, map.size);
    if(data.size() != data_size) {
        RCLCPP_ERROR(_logger, "Buffer size was not allocated correctly");
        gst_buffer_unref(buffer);
        return;
    }
    std::copy(message.data.begin(), message.data.end(), data.begin());
    gst_buffer_unmap (buffer, &map);

    //XXX: Use the stream delta to calculate our buffer timings
    const auto g_stream_stamp = common::ros_time_to_gst(stream_delta);
    const auto g_frame_stamp = common::ros_time_to_gst(frame_mark);
    const auto g_delta = common::ros_time_to_gst(frame_delta);

    GST_BUFFER_PTS(buffer) = g_stream_stamp;
    GST_BUFFER_DTS(buffer) = g_stream_stamp;
    GST_BUFFER_OFFSET(buffer) = g_stream_stamp;
    GST_BUFFER_DURATION(buffer) = g_delta;

    gst_buffer_add_reference_timestamp_meta(buffer, _gst.time_ref, g_frame_stamp, GST_CLOCK_TIME_NONE);

    const auto sample_in = gst_sample_new(buffer, caps, nullptr, nullptr);

    GstFlowReturn ret;
    g_signal_emit_by_name(_gst.source, "push-sample", sample_in, &ret);
    gst_sample_unref(sample_in);

    if(send_keyframe) {
        const auto pad = gst_element_get_static_pad(GST_ELEMENT(_gst.source), "sink");
        if(pad) {
            tooling::send_keyframe(pad, g_stream_stamp, g_stream_stamp, g_stream_stamp);
        } else {
            RCLCPP_ERROR(_logger, "Could not insert keyframe, pad not found");
        }
    }

    if (ret != GST_FLOW_OK) {
        RCLCPP_ERROR(_logger, "Could not push sample, frame dropped");
    }
}

bool GStreamerPublisher::_receive_sample(GstSample* sample) {
    gstreamer_image_transport::msg::DataPacket packet;
    //We stamp the packet with the current time, and we'll extract the image time from the encoded data
    //There is no guarantee that this packet is one image, a piece of an image, or something else
    packet.header.stamp = _node->get_clock()->now();

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

    _pub->publish(packet);

    return true;
}
};
