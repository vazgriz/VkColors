#pragma once
#include <vector>
#include <VulkanWrapper/VulkanWrapper.h>
#include <glm/glm.hpp>

namespace std {
    template<> struct hash<glm::ivec2> {
        size_t operator () (const glm::ivec2& v) const {
            size_t hash = 31 + std::hash<int>{}(v.x);
            hash = hash * 37 + std::hash<int>{}(v.y);
            return hash;
        }
    };
}

std::vector<char> loadFile(const std::string& path);
vk::ShaderModule loadShader(vk::Device& device, const std::string& path);
size_t align(size_t ptr, size_t align);
int32_t length2(glm::ivec3 v);