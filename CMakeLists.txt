cmake_minimum_required(VERSION 3.8)
project(gstreamer_image_transport)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()


# System Dependencies
find_package(PkgConfig)

pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)

find_package(ament_cmake REQUIRED)
# find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
# find_package(OpenCV REQUIRED COMPONENTS highgui imgcodecs imgproc videoio)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)
# find_package(sensor_msgs REQUIRED)
# find_package(std_msgs REQUIRED)

include_directories(
  include
  ${GLIB_INCLUDE_DIRS}
  ${GST_APP_INCLUDE_DIRS}
)

# add the resized image message
set(msg_files
  "msg/DataPacket.msg"
)
rosidl_generate_interfaces(${PROJECT_NAME}
  ${msg_files}
#   DEPENDENCIES sensor_msgs
)

rosidl_get_typesupport_target(cpp_typesupport_target "${PROJECT_NAME}" "rosidl_typesupport_cpp")

# add the plugin example
add_library(gstreamer_image_transport_plugins src/manifest.cpp src/gst_pub.cpp src/gst_sub.cpp)
target_link_libraries(gstreamer_image_transport_plugins
  "${cpp_typesupport_target}"
#   cv_bridge::cv_bridge
  image_transport::image_transport
#   opencv_imgproc
  pluginlib::pluginlib
  rclcpp::rclcpp
#   "${sensor_msgs_TARGETS}"
  ${GSTREAMER_LIBRARIES}
  ${GST_APP_LIBRARIES}
)

# Install plugin descriptions
pluginlib_export_plugin_description_file(${PROJECT_NAME} gstreamer_image_transport.xml)

# Install executables
install(
  TARGETS gstreamer_image_transport_plugins
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

# Install include directories
install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_dependencies(
    # cv_bridge
    image_transport
    pluginlib
    rosidl_default_runtime
    rclcpp
    # sensor_msgs
    # std_msgs
    GSTREAMER
    GST_APP
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
