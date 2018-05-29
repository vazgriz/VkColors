cmake_minimum_required(VERSION 3.9)

project(VkColors)

set(GLFW_INCLUDE)
set(GLFW_LIB)
set(VULKAN_INCLUDE)
set(VULKAN_LIB)
set(VKW_INCLUDE)
set(VKW_LIB)

add_executable(VkColors main.cpp core.cpp bitmap.cpp)
target_include_directories(VkColors PUBLIC ${GLFW_INCLUDE} ${VULKAN_INCLUDE} ${VKW_INCLUDE})
target_link_libraries(VkColors ${GLFW_LIB} ${VULKAN_LIB} ${VKW_LIB})