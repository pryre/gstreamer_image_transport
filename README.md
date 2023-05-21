# gstreamer_image_transport
GStreamer Image Transport handler


```
ros2 launch ./v4l.launch.xml
ros2 launch ./v4l.launch.xml GST_ENCODER:='jpegenc'
ros2 launch ./v4l.launch.xml GST_ENCODER:='videoconvert ! x264enc tune=zerolatency'
```
```
ros2 run image_transport republish gst raw --ros-args -r in/gst:=camera/image_raw/gst
```
```
ros2 topic echo /camera/image_raw/gst
ros2 run image_tools showimage --ros-args -r image:=/out
```