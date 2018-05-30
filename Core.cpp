#include "Core.h"
#include <set>
#include <iostream>

const std::vector<std::string> validationLayers = {
  "VK_LAYER_LUNARG_standard_validation"
};

const std::vector<std::string> deviceExtensions = {
    "VK_KHR_swapchain"
};

Core::Core(GLFWwindow* window) {
    m_window = window;

    createInstance();
    createSurface();
    selectPhysicalDevice();
    createDevice();
    createCommandPool();
    recreateSwapchain();
    createSemaphores();

    glfwSetWindowUserPointer(window, this);
    glfwSetWindowSizeCallback(window, &ResizeWindow);
    ResizeWindow(window, 0, 0);
    resizeFlag = false;
}

Core::Core(Core&& other) {
    *this = std::move(other);
}

void Core::ResizeWindow(GLFWwindow* window, int width, int height) {
    int fbWidth;
    int fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    Core* core = reinterpret_cast<Core*>(glfwGetWindowUserPointer(window));
    core->m_width = fbWidth;
    core->m_height = fbHeight;
    core->resizeFlag = true;
}

void Core::registerObserver(Observer* observer) {
    m_observers.push_back(observer);
}

void Core::acquire() {
    if (resizeFlag) {
        resizeFlag = false;
        m_device->waitIdle();
        recreateSwapchain();

        for (auto observer : m_observers) {
            observer->onResize(m_width, m_height);
        }
    }

    m_imageIndex = m_swapchain->acquireNextImage(~0, m_acquireSem.get(), nullptr);
    m_commandBuffer = &m_commandBuffers[m_imageIndex];
    m_fences[m_imageIndex].wait();
    m_fences[m_imageIndex].reset();

    m_commandBuffer->reset(vk::CommandBufferResetFlags::None);

    vk::CommandBufferBeginInfo beginInfo = {};
    beginInfo.flags = vk::CommandBufferUsageFlags::OneTimeSubmit;

    m_commandBuffer->begin(beginInfo);
}

vk::CommandBuffer& Core::getCommandBuffer() {
    return *m_commandBuffer;
}

void Core::present() {
    m_commandBuffer->end();

    vk::SubmitInfo submitInfo = {};
    submitInfo.commandBuffers = { *m_commandBuffer };
    submitInfo.waitSemaphores = { *m_acquireSem };
    submitInfo.waitDstStageMask = { vk::PipelineStageFlags::ColorAttachmentOutput };
    submitInfo.signalSemaphores = { *m_RenderSem };

    m_graphicsQueue->submit({ submitInfo }, &m_fences[m_imageIndex]);

    vk::PresentInfo presentInfo = {};
    presentInfo.imageIndices = { m_imageIndex };
    presentInfo.swapchains = { *m_swapchain };
    presentInfo.waitSemaphores = { *m_RenderSem };

    m_presentQueue->present(presentInfo);
}

vk::CommandBuffer Core::getSingleUseCommandBuffer() {
    vk::CommandBufferAllocateInfo info = {};
    info.commandPool = m_commandPool.get();
    info.commandBufferCount = 1;
    
    vk::CommandBuffer commandBuffer = std::move(m_commandPool->allocate(info)[0]);

    vk::CommandBufferBeginInfo beginInfo = {};
    beginInfo.flags = vk::CommandBufferUsageFlags::OneTimeSubmit;

    commandBuffer.begin(beginInfo);

    return commandBuffer;
}

void Core::submitSingleUseCommandBuffer(vk::CommandBuffer&& commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo info = {};
    info.commandBuffers = { commandBuffer };
    
    m_graphicsQueue->submit({ info }, nullptr);
    m_graphicsQueue->waitIdle();
}

void Core::createInstance() {
    vk::ApplicationInfo appInfo = {};
    appInfo.applicationName = "VkColors";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

    uint32_t extensionCount;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    std::vector<std::string> extensions;
    for (uint32_t i = 0; i < extensionCount; i++) {
        extensions.push_back(requiredExtensions[i]);
    }

    vk::InstanceCreateInfo info = {};
    info.applicationInfo = &appInfo;
    info.enabledExtensionNames = std::move(extensions);
    info.enabledLayerNames = validationLayers;

    m_instance = std::make_unique<vk::Instance>(info);
}

void Core::createSurface() {
    VkSurfaceKHR surface;
    VKW_CHECK(glfwCreateWindowSurface(m_instance->handle(), m_window, m_instance->callbacks(), &surface));
    m_surface = std::make_unique<vk::Surface>(*m_instance, surface);
}

bool Core::checkSwapchainSupport(const vk::PhysicalDevice& physicalDevice) {
    auto& available = physicalDevice.availableExtensions();
    std::set<std::string> requestedExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (auto& extension : available) {
        requestedExtensions.erase(extension.extensionName);
    }

    std::vector<vk::SurfaceFormat> formats = m_surface->getFormats(physicalDevice);
    std::vector<vk::PresentMode> modes = m_surface->getPresentModes(physicalDevice);

    return requestedExtensions.empty() && formats.size() > 0 && modes.size() > 0;
}

bool Core::isDeviceSuitable(const vk::PhysicalDevice& physicalDevice) {
    if (!checkSwapchainSupport(physicalDevice)) return false;

    bool graphicsFound = false;
    bool computeFound = false;
    bool presentFound = false;

    auto& families = physicalDevice.queueFamilies();
    for (uint32_t i = 0; i < families.size(); i++) {
        auto& queueFamily = families[i];
        if (!graphicsFound && queueFamily.queueCount > 0 && (queueFamily.queueFlags & vk::QueueFlags::Graphics) != vk::QueueFlags::None) {
            graphicsFound = true;
            m_graphicsQueueIndex = i;
        }

        if (!presentFound && m_surface->supported(physicalDevice, i)) {
            presentFound = true;
            m_presentQueueIndex = i;
        }

        if (!computeFound && queueFamily.queueCount > 0
            && (queueFamily.queueFlags & vk::QueueFlags::Compute) != vk::QueueFlags::None
            && (queueFamily.queueFlags & vk::QueueFlags::Graphics) == vk::QueueFlags::None) {
            computeFound = true;
            m_computeQueueIndex = i;
        }

        if (graphicsFound && presentFound && computeFound) break;
    }

    if (!computeFound) {
        m_computeQueueIndex = m_graphicsQueueIndex;
    }

    return graphicsFound && presentFound;
}

void Core::selectPhysicalDevice() {
    auto& physicalDevices = m_instance->physicalDevices();
    if (physicalDevices.size() == 0) throw std::runtime_error("Failed to find physical devices");

    for (auto& physicalDevice : physicalDevices) {
        if (isDeviceSuitable(physicalDevice)) {
            this->m_physicalDevice = &physicalDevice;
            break;
        }
    }

    if (m_physicalDevice == nullptr) {
        throw std::runtime_error("Failed to find a suitable physical device");
    }
}

void Core::createDevice() {
    std::set<uint32_t> indices = { m_graphicsQueueIndex, m_presentQueueIndex, m_computeQueueIndex };
    std::vector<vk::DeviceQueueCreateInfo> queueInfos;

    bool sharedFamily = false;
    if (m_graphicsQueueIndex == m_computeQueueIndex && m_physicalDevice->queueFamilies()[m_graphicsQueueIndex].queueCount > 1) {
        sharedFamily = true;
    }

    for (auto i : indices) {
        vk::DeviceQueueCreateInfo info = {};
        info.queueFamilyIndex = i;
        if (sharedFamily && i == m_graphicsQueueIndex) {
            info.queueCount = 2;
            info.queuePriorities = { 1, 1 };
        } else {
            info.queueCount = 1;
            info.queuePriorities = { 1 };
        }
        queueInfos.push_back(info);
    }

    vk::PhysicalDeviceFeatures deviceFeatures = {};

    vk::DeviceCreateInfo info = {};
    info.queueCreateInfos = queueInfos;
    info.enabledFeatures = &deviceFeatures;
    info.enabledExtensionNames = deviceExtensions;

    m_device = std::make_unique<vk::Device>(*m_physicalDevice, info);

    m_graphicsQueue = &m_device->getQueue(m_graphicsQueueIndex, 0);
    m_presentQueue = &m_device->getQueue(m_presentQueueIndex, 0);
    m_computeQueue = &m_device->getQueue(m_computeQueueIndex, sharedFamily ? 1 : 0);
}

void Core::createCommandPool() {
    vk::CommandPoolCreateInfo info = {};
    info.queueFamilyIndex = m_graphicsQueueIndex;
    info.flags = vk::CommandPoolCreateFlags::ResetCommandBuffer;

    m_commandPool = std::make_unique<vk::CommandPool>(*m_device, info);
}

void Core::recreateSwapchain() {
    createSwapchain();
    createImageViews();
    createRenderPass();
    createFramebuffers();
    createCommandBuffers();
    createFences();
}

vk::SurfaceFormat Core::chooseSurfaceFormat(const std::vector<vk::SurfaceFormat>& formats) {
    if (formats.size() == 1 && formats[0].format == vk::Format::Undefined) {
        return { vk::Format::R8G8B8A8_Unorm, vk::ColorSpace::SrgbNonlinear };
    }

    for (auto& format : formats) {
        if (format.colorSpace == vk::ColorSpace::SrgbNonlinear
            && (format.format == vk::Format::R8G8B8A8_Unorm || format.format == vk::Format::B8G8R8A8_Unorm)) {
            return format;
        }
    }

    return formats[0];
}

vk::PresentMode Core::choosePresentMode(const std::vector<vk::PresentMode>& modes) {
    for (auto mode : modes) {
        if (mode == vk::PresentMode::Fifo) {
            return mode;
        }
    }

    for (auto mode : modes) {
        if (mode == vk::PresentMode::Immediate) {
            return mode;
        }
    }

    throw std::runtime_error("Failed to find presentation mode");
}

vk::Extent2D Core::chooseExtent(const vk::SurfaceCapabilities& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };

        actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

        return actualExtent;
    }
}

void Core::createSwapchain() {
    auto capabilities = m_surface->getCapabilities(*m_physicalDevice);
    auto formats = m_surface->getFormats(*m_physicalDevice);
    auto presentModes = m_surface->getPresentModes(*m_physicalDevice);

    auto format = chooseSurfaceFormat(formats);
    auto presentMode = choosePresentMode(presentModes);
    auto extent = chooseExtent(capabilities);

    uint32_t imageCount = 2;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfo info = {};
    info.surface = m_surface.get();
    info.flags = vk::SwapchainCreateFlags::None;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = vk::ImageUsageFlags::ColorAttachment;

    if (m_graphicsQueue->familyIndex() != m_presentQueue->familyIndex()) {
        info.imageSharingMode = vk::SharingMode::Concurrent;
        info.queueFamilyIndices = { m_graphicsQueueIndex, m_presentQueueIndex };
    } else {
        info.imageSharingMode = vk::SharingMode::Exclusive;
    }

    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = vk::CompositeAlphaFlags::Opaque;
    info.presentMode = presentMode;
    info.clipped = true;
    info.oldSwapchain = m_swapchain.get();

    m_swapchain = std::make_unique<vk::Swapchain>(*m_device, info);
}

vk::ImageView Core::createImageView(vk::Image& image, vk::Format format) {
    vk::ImageViewCreateInfo info = {};
    info.image = &image;
    info.viewType = vk::ImageViewType::_2D;
    info.format = format;
    info.subresourceRange.aspectMask = vk::ImageAspectFlags::Color;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;

    return vk::ImageView(*m_device, info);
}

void Core::createImageViews() {
    m_swapchainImageViews.clear();
    m_swapchainImageViews.reserve(m_swapchain->images().size());
    for (auto& image : m_swapchain->images()) {
        m_swapchainImageViews.emplace_back(createImageView(image, m_swapchain->format()));
    }
}

void Core::createRenderPass() {
    vk::AttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapchain->format();
    colorAttachment.samples = vk::SampleCountFlags::_1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::Clear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::Store;
    colorAttachment.initialLayout = vk::ImageLayout::Undefined;
    colorAttachment.finalLayout = vk::ImageLayout::PresentSrcKhr;

    vk::AttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::ColorAttachmentOptimal;

    vk::SubpassDescription subpass = {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::Graphics;
    subpass.colorAttachments = { colorAttachmentRef };

    vk::RenderPassCreateInfo info = {};
    info.attachments = { colorAttachment };
    info.subpasses = { subpass };

    m_renderPass = std::make_unique<vk::RenderPass>(*m_device, info);
}

void Core::createFramebuffers() {
    m_framebuffers.clear();
    m_framebuffers.reserve(m_swapchain->images().size());

    for (size_t i = 0; i < m_swapchain->images().size(); i++) {
        vk::FramebufferCreateInfo info = {};
        info.renderPass = m_renderPass.get();
        info.attachments = { m_swapchainImageViews[i] };
        info.width = m_swapchain->extent().width;
        info.height = m_swapchain->extent().height;
        info.layers = 1;

        m_framebuffers.emplace_back(*m_device, info);
    }
}

void Core::createCommandBuffers() {
    vk::CommandBufferAllocateInfo info = {};
    info.commandPool = m_commandPool.get();
    info.commandBufferCount = static_cast<uint32_t>(m_swapchain->images().size());

    m_commandBuffers = m_commandPool->allocate(info);

    for (auto& commandBuffer : m_commandBuffers) {
        commandBuffer.setDestructorEnabled(true);
    }
}

void Core::createFences() {
    m_fences.clear();
    m_fences.reserve(m_swapchain->images().size());

    for (auto& image : m_swapchain->images()) {
        vk::FenceCreateInfo info = {};
        info.flags = vk::FenceCreateFlags::Signaled;

        m_fences.emplace_back(*m_device, info);
    }
}

void Core::createSemaphores() {
    vk::SemaphoreCreateInfo info = {};
    
    m_acquireSem = std::make_unique<vk::Semaphore>(*m_device, info);
    m_RenderSem = std::make_unique<vk::Semaphore>(*m_device, info);
}

void Core::beginRenderPass(vk::CommandBuffer& commandBuffer) {
    vk::RenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.renderPass = m_renderPass.get();
    renderPassInfo.framebuffer = &m_framebuffers[m_imageIndex];
    renderPassInfo.renderArea = { {}, { m_swapchain->extent().width, m_swapchain->extent().height } };

    vk::ClearValue clearColor = {};
    clearColor.color.float32[0] = 0.125f;
    clearColor.color.float32[1] = 0.125f;
    clearColor.color.float32[2] = 0.125f;
    renderPassInfo.clearValues = { clearColor };

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::Inline);
}