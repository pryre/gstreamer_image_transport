#include "gstreamer_image_transport/gst_sub.hpp"

#include <memory>

// #include "cv_bridge/cv_bridge.h"
// #include "opencv2/core/mat.hpp"
// #include "opencv2/imgproc.hpp"

namespace gstreamer_image_transport
{

void GStreamerSubscriber::internalCallback(
  const gstreamer_image_transport::msg::DataPacket::ConstSharedPtr & msg,
  const Callback & user_cb) {
//   // This is only for optimization, not to copy the image
//   std::shared_ptr<void const> tracked_object_tmp;
//   cv::Mat img_rsz = cv_bridge::toCvShare(msg->image, tracked_object_tmp)->image;
//   // Resize the image to its original size
//   cv::Mat img_restored;
//   cv::resize(img_rsz, img_restored, cv::Size(msg->original_width, msg->original_height));

//   // Call the user callback with the restored image
//   cv_bridge::CvImage cv_img(msg->image.header, msg->image.encoding, img_restored);
//   user_cb(cv_img.toImageMsg());
}

};
