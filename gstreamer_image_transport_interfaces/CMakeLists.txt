cmake_minimum_required(VERSION 3.8)
project(gstreamer_image_transport_interfaces)

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

# ament_auto_generate_code()
file(GLOB ${PROJECT_NAME}_msg_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} msg/*)
file(GLOB ${PROJECT_NAME}_srv_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} srv/*)
rosidl_generate_interfaces(
   ${PROJECT_NAME}
   ${${PROJECT_NAME}_msg_files}
   ${${PROJECT_NAME}_srv_files}
   DEPENDENCIES ${${PROJECT_NAME}_FOUND_BUILD_DEPENDS}
)

ament_auto_package()
