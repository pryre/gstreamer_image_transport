ros2 run gscam gscam_node --ros-args -p gscam_config:='v4l2src ! image/jpeg,width=800,height=600,framerate=15/1 ! vaapijpegdec ! videoconvert' -p gst_pub.pipeline:='videorate ! video/x-raw,framerate=15/1 ! videoconvert ! vaapih264enc quality-level=7'
ros2 run gscam gscam_node --ros-args -p gscam_config:='v4l2src ! video/x-raw,width=1280,height=800,framerate=10/1 ! videoconvert' -p gst_pub.pipeline:='videorate ! video/x-raw,framerate=10/1 ! videoconvert ! vaapih264enc quality-level=7 ! h264parse config-interval=-1'


ros2 run image_transport republish gst raw --ros-args -r in/gst:=camera/image_raw/gst -p gst_sub.pipeline:='vaapidecodebin ! videoconvert ! video/x-raw,format=RGB'





gst-launch-1.0 v4l2src ! image/jpeg,width=1280,height=800,framerate=15/1 ! jpegdec ! video/x-raw,format=RGB ! tee name=t ! queue leaky=2 ! shmsink socket-path=/tmp/camera_front wait-for-connection=false sync=false t. ! queue leaky=2 ! videoconvert ! vaapih264enc quality-level=7 ! rtph264pay ! udpsink host=224.224.224.1 port=5000 auto-multicast=true
gst-launch-1.0 shmsrc socket-path=/tmp/camera_front is-live=true do-timestamp=true ! video/x-raw,width=1280,height=800,framerate=15/1,format=RGB ! videoconvert ! autovideosink sync=false
gst-launch-1.0 udpsrc multicast-group=224.224.224.1 auto-multicast=true port=5000 ! application/x-rtp ! rtph264depay ! vaapih264dec low-latency=true ! autovideosink sync=false

ros2 run gscam gscam_node --ros-args -p gscam_config:='shmsrc socket-path=/tmp/camera_front is-live=true do-timestamp=true ! video/x-raw,framerate=15/1,format=RGB,width=1280,height=800 ! videoconvert' -p sync_sink:=false



gst-launch-1.0 v4l2src ! image/jpeg,width=1280,height=720,framerate=30/1 ! jpegdec ! videoconvert ! vaapih264enc quality-level=7 ! h264parse config-interval=-1 ! mpegtsmux ! udpsink host=127.0.0.1 port=5000
gst-launch-1.0 udpsrc port=5000 ! tsdemux ! vaapih264dec low-latency=true ! autovideosink sync=false



gst-launch-1.0 libcamerasrc ! video/x-raw,width=1920,height=1080,framerate=30/1,format=I420,interlace-mode=progressive ! v4l2h264enc ! video/x-h264,level=\(string\)4,profile=main ! h264parse config-interval=-1 ! mpegtsmux ! udpsink host=127.0.0.1 port=5000 sync=false
gst-launch-1.0 libcamerasrc ! video/x-raw,width=1920,height=1080,framerate=30/1,format=I420,interlace-mode=progressive ! v4l2h264enc ! video/x-h264,level=\(string\)4,profile=main ! h264parse config-interval=-1 ! rtph264pay ! udpsink host=224.224.224.1 port=5000 auto-multicast=true sync=false
gst-launch-1.0 udpsrc multicast-group=224.224.224.1 auto-multicast=true port=5000 ! application/x-rtp ! rtph264depay ! fakesink


#Normal Camera
gst-launch-1.0 libcamerasrc ! video/x-raw,width=1920,height=1080,framerate=30/1,format=I420,interlace-mode=progressive ! v4l2h264enc extra-controls=encode,h264_minimum_qp_value=32,repeat_sequence_header=1 ! video/x-h264,level=\(string\)4,profile=main ! mpegtsmux ! udpsink host=224.224.224.1 port=5000 auto-multicast=true sync=false
#HQ Camera
gst-launch-1.0 libcamerasrc ! video/x-raw,colorimetry=bt709,interlace-mode=progressive,width=1280,height=720,framerate=30/1,format=NV12 ! v4l2h264enc extra-controls=encode,h264_minimum_qp_value=32,repeat_sequence_header=1 ! video/x-h264,level=\(string\)4,profile=main ! mpegtsmux ! udpsink host=224.224.224.1 port=5000 auto-multicast=true sync=false
#Reciever
gst-launch-1.0 udpsrc multicast-group=224.224.224.1 auto-multicast=true port=5000 !  video/mpegts ! tsdemux ! decodebin ! autovideosink sync=false
#tsdemux0: CONTINUITY: Mismatch packet ###
#^^^^^^^^ may be solved by adding a larger buffer to the reciever
