#include "Utilities.h"
#include <fstream>
#include <stdexcept>

std::vector<char> loadFile(const std::string& path) {
    std::ifstream file = std::ifstream(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Could not open file");

    std::vector<char> result;
    result.resize(file.tellg());
    file.seekg(0, std::ios::beg);

    file.read(result.data(), result.size());

    return result;
}

vk::ShaderModule loadShader(vk::Device& device, const std::string& path) {
    std::vector<char> data = loadFile(path);

    vk::ShaderModuleCreateInfo info = {};
    info.code = std::move(data);
    
    return vk::ShaderModule(device, info);
}