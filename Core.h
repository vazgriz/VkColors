#pragma once
#include <VulkanWrapper/VulkanWrapper.h>
#include <GLFW/glfw3.h>
#include <memory>

class Core {
public:
    Core(GLFWwindow* window, int width, int height);
    Core(const Core& other) = delete;
    Core& operator = (const Core& other) = delete;
    Core(Core&& other);
    Core& operator = (Core&& other) = default;
    ~Core();

    uint32_t imageIndex() { return m_imageIndex; }
    void acquire();
    vk::CommandBuffer& getCommandBuffer();
    void present();

    vk::Device& device() { return *m_device; }
    vk::Swapchain& swapchain() { return *m_swapchain; }

private:
    GLFWwindow* m_window;
    int m_width;
    int m_height;
    bool resizeFlag = false;
    std::unique_ptr<vk::Instance> m_instance;
    const vk::PhysicalDevice* m_physicalDevice = nullptr;
    std::unique_ptr<vk::Surface> m_surface;
    uint32_t m_graphicsQueueIndex;
    uint32_t m_computeQueueIndex;
    uint32_t m_presentQueueIndex;
    std::unique_ptr<vk::Device> m_device;
    const vk::Queue* m_graphicsQueue;
    const vk::Queue* m_computeQueue;
    const vk::Queue* m_presentQueue;
    std::unique_ptr<vk::CommandPool> m_commandPool;
    std::unique_ptr<vk::Swapchain> m_swapchain;
    std::vector<vk::ImageView> m_swapchainImageViews;
    std::unique_ptr<vk::RenderPass> m_renderPass;
    std::vector<vk::Framebuffer> m_framebuffers;
    std::vector<vk::CommandBuffer> m_commandBuffers;
    std::vector<vk::Fence> m_fences;
    std::unique_ptr<vk::Semaphore> m_acquireSem;
    std::unique_ptr<vk::Semaphore> m_RenderSem;

    uint32_t m_imageIndex;
    vk::CommandBuffer* m_commandBuffer;

    static void ResizeWindow(GLFWwindow* window, int width, int height);

    void createInstance();
    void selectPhysicalDevice();
    bool checkSwapchainSupport(const vk::PhysicalDevice& physicalDevice);
    bool isDeviceSuitable(const vk::PhysicalDevice& physicalDevice);
    void createSurface();
    void createDevice();
    void createCommandPool();
    void recreateSwapchain();
    vk::SurfaceFormat chooseSurfaceFormat(const std::vector<vk::SurfaceFormat>& formats);
    vk::PresentMode choosePresentMode(const std::vector<vk::PresentMode>& modes);
    vk::Extent2D chooseExtent(const vk::SurfaceCapabilities& capabilities);
    void createSwapchain();
    vk::ImageView createImageView(vk::Image& image, vk::Format format);
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandBuffers();
    void createFences();
    void createSemaphores();
    void recordCommands(vk::CommandBuffer& commandBuffer);
};