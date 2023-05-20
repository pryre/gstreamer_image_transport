#include "gstreamer_image_transport/gst_pub.hpp"

#include <memory>

// #include "cv_bridge/cv_bridge.h"
// #include "opencv2/core/mat.hpp"
// #include "opencv2/imgproc.hpp"
#include "rclcpp/logging.hpp"


GStreamerPublisher::GStreamerPublisher() :
    gst_config_(""),
    gst_pipeline_(NULL),
    gst_src_(NULL),
    gst_sink_(NULL)
{

}

void GStreamerPublisher::advertiseImpl(rclcpp::Node * node, const std::string & base_topic, rmw_qos_profile_t custom_qos) {
    //Do implicit advertising
    SimplePublisherPlugin::advertiseImpl(node, base_topic, custom_qos);

    // Get gstreamer configuration
    const auto gst_config_internals = node->declare_parameter("gscam_config", "");
    gst_config = "appsrc ! " + gst_config_internals ?

    RCLCPP_INFO_STREAM(
        get_logger(),
    "Using gstreamer config from rosparam: \"" << gsconfig_rosparam << "\"");
    }
}

GStreamerPublisher::~GStreamerPublisher() {

}

void GStreamerPublisher::publish(
  const sensor_msgs::msg::Image & message,
  const PublishFn & publish_fn) const
{
//   cv::Mat cv_image;
//   std::shared_ptr<void const> tracked_object;
//   try {
//     cv_image = cv_bridge::toCvShare(message, tracked_object, message.encoding)->image;
//   } catch (const cv::Exception & e) {
//     auto logger = rclcpp::get_logger("gst_pub");
//     RCLCPP_ERROR(
//       logger, "Could not convert from '%s' to '%s'.",
//       message.encoding.c_str(), message.encoding.c_str());
//     return;
//   }

//   // Rescale image
//   double subsampling_factor = 2.0;
//   int new_width = cv_image.cols / subsampling_factor + 0.5;
//   int new_height = cv_image.rows / subsampling_factor + 0.5;
//   cv::Mat buffer;
//   cv::resize(cv_image, buffer, cv::Size(new_width, new_height));

//   // Set up ResizedImage and publish
//   image_transport_tutorials::msg::ResizedImage resized_image;
//   resized_image.original_height = cv_image.rows;
//   resized_image.original_width = cv_image.cols;
//   resized_image.image = *(cv_bridge::CvImage(message.header, "bgr8", cv_image).toImageMsg());
//   publish_fn(resized_image);
}
