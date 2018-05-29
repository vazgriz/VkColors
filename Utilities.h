#include <vector>
#include <VulkanWrapper/VulkanWrapper.h>

std::vector<char> loadFile(const std::string& path);
vk::ShaderModule loadShader(vk::Device& device, const std::string& path);