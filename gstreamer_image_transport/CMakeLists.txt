cmake_minimum_required(VERSION 3.8)
project(gstreamer_image_transport)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_auto REQUIRED)
ament_auto_find_build_dependencies()

find_package(PkgConfig)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)

include_directories(
  include
  ${GLIB_INCLUDE_DIRS}
  ${GST_APP_INCLUDE_DIRS}
)

# rosidl_get_typesupport_target(cpp_typesupport_target "${PROJECT_NAME}" "rosidl_typesupport_cpp")

set(LIBRARY_NAME ${PROJECT_NAME}_component)

# add the plugin example
ament_auto_add_library(${LIBRARY_NAME} SHARED src/gst_pub.cpp src/gst_sub.cpp src/manifest.cpp)
# ament_add_library(${LIBRARY_NAME} SHARED src/gst_pub.cpp src/gst_sub.cpp src/manifest.cpp)
target_link_libraries(${LIBRARY_NAME}
#   "${cpp_typesupport_target}"
#   image_transport::image_transport
#   pluginlib::pluginlib
#   rclcpp::rclcpp
#   "${std_msgs_TARGETS}"
#   "${sensor_msgs_TARGETS}"
#   "${diagnostic_updater_TARGETS}"
  ${GSTREAMER_LIBRARIES}
  ${GST_APP_LIBRARIES}
)

# ament_export_dependencies(
#     # cv_bridge
#     image_transport
#     pluginlib
#     rosidl_default_runtime
#     rclcpp
#     sensor_msgs
#     std_msgs
#     diagnostic_updater
#     GSTREAMER
#     GST_APP
# )

install(TARGETS ${LIBRARY_NAME} EXPORT export_${LIBRARY_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# ament_export_targets(export_${LIBRARY_NAME})

# install(
#   DIRECTORY include/
#   DESTINATION include/${PROJECT_NAME}
# )

pluginlib_export_plugin_description_file(image_transport ${PROJECT_NAME}.xml)

# ament_package()

ament_auto_package(
  INSTALL_TO_SHARE launch config
  # INSTALL_TO_INCLUDE include
)
