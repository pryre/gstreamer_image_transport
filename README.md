# Gstreamer Image Transport

GStreamer Image Transport handler

```sh
ros2 launch ./v4l.launch.xml
ros2 launch ./v4l.launch.xml GST_ENCODER:='jpegenc'
ros2 launch ./v4l.launch.xml GST_ENCODER:='videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast'

ros2 run gscam gscam_node --ros-args -p gscam_config:='v4l2src device=/dev/video0 ! video/x-raw,framerate=30/1 ! videoconvert' -p gst_pub.pipeline:='videorate ! video/x-raw,framerate=10/1 ! videoconvert ! vaapih264enc'
ros2 run gscam gscam_node --ros-args -p gscam_config:='v4l2src device=/dev/video0 ! video/x-raw,framerate=30/1 ! videoconvert' -p gst_pub.pipeline:='videorate ! video/x-raw,framerate=10/1 ! videoconvert ! vaapih264enc ! rtph264pay'
```

```sh
ros2 run image_transport republish gst raw --ros-args -r in/gst:=camera/image_raw/gst
ros2 run image_transport republish gst raw --ros-args -r in/gst:=camera/image_raw/gst -p gst_sub.pipeline:='decodebin ! videoconvert ! video/x-raw,format=RGB'
```

```sh
ros2 topic echo /camera/image_raw/gst
ros2 run image_tools showimage --ros-args -r image:=/out
```

Attempt 2:

```sh
# Raw
ros2 run gscam gscam_node --ros-args -p gscam_config:='v4l2src device=/dev/video0 ! video/x-raw,framerate=30/1 ! videoconvert'
# Encoded
ros2 run gscam gscam_node --ros-args -p gscam_config:='v4l2src device=/dev/video0 ! video/x-raw,framerate=30/1 ! videoconvert' -p gst_pub.pipeline:='videorate ! video/x-raw,framerate=10/1 ! videoconvert ! vaapih264enc'
# Raspberry Pi
ros2 run gscam gscam_node --ros-args -p gscam_config:='libcamerasrc ! video/x-raw,framerate=30/1 ! videoconvert' -p gst_pub.pipeline:='videorate ! video/x-raw,framerate=10/1 ! videoconvert ! v4l2h264enc ! video/x-h264,level=(string)3'
```

```sh
ros2 run image_transport republish gst raw --ros-args -r in/gst:=camera/image_raw/gst -p gst_sub.pipeline:='decodebin ! videoconvert ! video/x-raw,format=RGB'
```

```sh
ros2 run image_tools showimage --ros-args -r image:=/out
ros2 topic bw /camera/image_raw/gst
ros2 topic delay /out
```
