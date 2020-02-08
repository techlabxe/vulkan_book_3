#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_win32.h>

#include "Swapchain.h"

template<class T>
class VulkanObjectStore
{
public:
  VulkanObjectStore(std::function<void(T)> disposer) : m_disposeFunc(disposer) { }
  void Cleanup() {
    std::for_each(m_storeMap.begin(), m_storeMap.end(), [&](auto v) { m_disposeFunc(v.second); });
  }

  void Register(const std::string& name, T data)
  {
    m_storeMap[name] = data;
  }
  T Get(const std::string& name) const
  {
    auto it = m_storeMap.find(name);
    if (it == m_storeMap.end())
    {
      return VK_NULL_HANDLE;
    }
    return it->second;
  }
private:
  std::unordered_map<std::string, T> m_storeMap;
  std::function<void(T)> m_disposeFunc;
};

class VulkanAppBase {
public:
  VulkanAppBase() :m_isMinimizedWindow(false), m_isFullscreen(false) { }
  virtual ~VulkanAppBase() { }

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);
  virtual bool OnMouseButtonDown(int button);
  virtual bool OnMouseButtonUp(int button);
  virtual bool OnMouseMove(int dx, int dy);

  uint32_t GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const;
  void SwitchFullscreen(GLFWwindow* window);

  void Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen);
  void Terminate();

  virtual void Render() = 0;
  virtual void Prepare() = 0;
  virtual void Cleanup() = 0;

  VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }
  VkDevice GetDevice() { return m_device; }
  const Swapchain* GetSwapchain() const { return m_swapchain.get(); }

  VkPipelineLayout GetPipelineLayout(const std::string& name) { return m_pipelineLayoutStore->Get(name); }
  VkDescriptorSetLayout GetDescriptorSetLayout(const std::string& name) { return m_descriptorSetLayoutStore->Get(name); }
  VkRenderPass GetRenderPass(const std::string& name) { return m_renderPassStore->Get(name); }

  void RegisterLayout(const std::string& name, VkPipelineLayout layout) { m_pipelineLayoutStore->Register(name, layout); }
  void RegisterLayout(const std::string& name, VkDescriptorSetLayout layout) { m_descriptorSetLayoutStore->Register(name, layout); }
  void RegisterRenderPass(const std::string& name, VkRenderPass renderPass) { m_renderPassStore->Register(name, renderPass); }
  struct BufferObject
  {
    VkBuffer buffer;
    VkDeviceMemory memory;
  };
  struct ImageObject
  {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
  };

  BufferObject CreateBuffer(uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
  ImageObject CreateTexture(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage);
  VkFramebuffer CreateFramebuffer(VkRenderPass renderPass, uint32_t width, uint32_t height, uint32_t viewCount, VkImageView* views);
  VkFence CreateFence();
  VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout dsLayout);

  void DestroyBuffer(BufferObject bufferObj);
  void DestroyImage(ImageObject imageObj);
  void DestroyFramebuffers(uint32_t count, VkFramebuffer* framebuffers);
  void DestroyFence(VkFence fence);
  void DeallocateDescriptorSet(VkDescriptorSet dsLayout);

  VkCommandBuffer CreateCommandBuffer(bool bBegin = true);
  void FinishCommandBuffer(VkCommandBuffer command);
  void DestroyCommandBuffer(VkCommandBuffer command);

  VkRect2D GetSwapchainRenderArea() const;

  std::vector<BufferObject> CreateUniformBuffers(uint32_t size, uint32_t imageCount);

  // ホストから見えるメモリ領域にデータを書き込む.以下バッファを対象に使用.
  // - ステージングバッファ
  // - ユニフォームバッファ
  void WriteToHostVisibleMemory(VkDeviceMemory memory, uint32_t size, const void* pData);

  void AllocateCommandBufferSecondary(uint32_t count, VkCommandBuffer* pCommands);
  void FreeCommandBufferSecondary(uint32_t count, VkCommandBuffer* pCommands);

  void TransferStageBufferToImage(const BufferObject& srcBuffer, const ImageObject& dstImage, const VkBufferImageCopy* region);


  // レンダーパスの生成.
  VkRenderPass CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat = VK_FORMAT_UNDEFINED, VkImageLayout layoutColor = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

  struct ModelData
  {
    uint32_t indexCount;
    uint32_t vertexCount;
    BufferObject resVertexBuffer;
    BufferObject resIndexBuffer;
  };

  // 単純モデルのデータをGPUへ転送.
  template<class T>
  ModelData CreateSimpleModel(const std::vector<T>& vertices, const std::vector<uint32_t>& indices)
  {
    ModelData model;
    VkMemoryPropertyFlags srcMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryPropertyFlags dstMemoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBufferCopy copyVB{}, copyIB{};

    auto bufferSize = uint32_t(sizeof(T) * vertices.size());
    auto uploadVB = CreateBuffer(bufferSize, usageVB, srcMemoryProps);
    model.resVertexBuffer = CreateBuffer(bufferSize, usageVB, dstMemoryProps);
    WriteToHostVisibleMemory(uploadVB.memory, bufferSize, vertices.data());
    model.vertexCount = uint32_t(vertices.size());
    copyVB.size = bufferSize;

    bufferSize = uint32_t(sizeof(uint32_t) * indices.size());
    auto uploadIB = CreateBuffer(bufferSize, usageIB, srcMemoryProps);
    model.resIndexBuffer = CreateBuffer(bufferSize, usageIB, dstMemoryProps);
    WriteToHostVisibleMemory(uploadIB.memory, bufferSize, indices.data());
    model.indexCount = uint32_t(indices.size());
    copyIB.size = bufferSize;

    auto command = CreateCommandBuffer();
    vkCmdCopyBuffer(command, uploadVB.buffer, model.resVertexBuffer.buffer, 1, &copyVB);
    vkCmdCopyBuffer(command, uploadIB.buffer, model.resIndexBuffer.buffer, 1, &copyIB);
    FinishCommandBuffer(command);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
    DestroyBuffer(uploadVB);
    DestroyBuffer(uploadIB);

    return model;
  }

 private:
  void CreateInstance();
  void SelectGraphicsQueue();
  void CreateDevice();
  void CreateCommandPool();

  // デバッグレポート有効化.
  void EnableDebugReport();
  void DisableDebugReport();
  PFN_vkCreateDebugReportCallbackEXT	m_vkCreateDebugReportCallbackEXT;
  PFN_vkDebugReportMessageEXT	m_vkDebugReportMessageEXT;
  PFN_vkDestroyDebugReportCallbackEXT m_vkDestroyDebugReportCallbackEXT;
  VkDebugReportCallbackEXT  m_debugReport;

  void CreateDescriptorPool();

  // ImGui
  void PrepareImGui();
  void CleanupImGui();
protected:
  VkDeviceMemory AllocateMemory(VkBuffer image, VkMemoryPropertyFlags memProps);
  VkDeviceMemory AllocateMemory(VkImage image, VkMemoryPropertyFlags memProps);
  // 最小化メッセージループ.
  void MsgLoopMinimizedWindow();

  VkDevice  m_device;
  VkPhysicalDevice m_physicalDevice;
  VkInstance m_vkInstance;

  VkPhysicalDeviceMemoryProperties m_physicalMemProps;
  VkQueue m_deviceQueue;
  uint32_t  m_gfxQueueIndex;
  VkCommandPool m_commandPool;

  VkSemaphore m_renderCompletedSem, m_presentCompletedSem;

  VkDescriptorPool m_descriptorPool;

  bool m_isMinimizedWindow;
  bool m_isFullscreen;
  std::unique_ptr<Swapchain> m_swapchain;
  GLFWwindow* m_window;

  using RenderPassRegistry = VulkanObjectStore<VkRenderPass>;
  using PipelineLayoutManager = VulkanObjectStore<VkPipelineLayout>;
  using DescriptorSetLayoutManager = VulkanObjectStore<VkDescriptorSetLayout>;
  std::unique_ptr<RenderPassRegistry> m_renderPassStore;
  std::unique_ptr<PipelineLayoutManager> m_pipelineLayoutStore;
  std::unique_ptr<DescriptorSetLayoutManager> m_descriptorSetLayoutStore;
};