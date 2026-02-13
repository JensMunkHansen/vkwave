#pragma once

#include <vkwave/config.h>

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkwave
{
/**
        Read a file.

        \param filename a string representing the path to the file
        \param debug whether the system is running in debug mode
        \returns the contents as a vector of raw binary characters
*/
std::vector<char> readFile(std::string filename, bool debug);

/**
        Make a shader module.

        \param filename a string holding the filepath to the spir-v file.
        \param device the logical device
        \param debug whether the system is running in debug mode
        \returns the created shader module
*/

vk::ShaderModule createModule(std::string filename, vk::Device device, bool debug);
}
