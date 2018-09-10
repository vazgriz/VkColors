cmake_minimum_required(VERSION 3.9)

project(VkColors)

set(GLFW_INCLUDE)
set(GLFW_LIB)
set(VULKAN_INCLUDE)
set(VULKAN_LIB)
set(VKW_INCLUDE)
set(VKW_LIB)
set(GLM_INCLUDE)
set(GLSL_VALIDATOR)

set(SHADER_SOURCES
    "${PROJECT_SOURCE_DIR}/shaders/shader.vert" ;
    "${PROJECT_SOURCE_DIR}/shaders/shader.frag" ;
    "${PROJECT_SOURCE_DIR}/shaders/wave.comp" ;
    "${PROJECT_SOURCE_DIR}/shaders/coral.comp" ;
    "${PROJECT_SOURCE_DIR}/shaders/update.comp" ;
)
set(SPIRV_BINARY_FILES)

foreach(SHADER_SOURCE ${SHADER_SOURCES})
    get_filename_component(FILE_NAME ${SHADER_SOURCE} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSL_VALIDATOR} ${SHADER_SOURCE} -o ${SPIRV}
        DEPENDS ${SHADER_SOURCE})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(SHADER_SOURCE)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
)

add_executable(VkColors
    main.cpp
    Core.cpp
    Bitmap.cpp
    Allocator.cpp
    Utilities.cpp
    Renderer.cpp
    ShuffleSource.cpp
    ComputeGenerator.cpp
    WaveGenerator.cpp
    WaveGenerator2.cpp
    CoralGenerator.cpp
    Staging.cpp
    ColorQueue.cpp
)
target_include_directories(VkColors PUBLIC ${GLFW_INCLUDE} ${VULKAN_INCLUDE} ${VKW_INCLUDE} ${GLM_INCLUDE})
target_link_libraries(VkColors ${GLFW_LIB} ${VULKAN_LIB} ${VKW_LIB})
add_dependencies(VkColors Shaders)