#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include <array>
#include "Camera.h"

class ComputeFilterApp : public VulkanAppBase
{
public:
  ComputeFilterApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);

  struct ShaderParameters
  {
    glm::mat4 proj;
  };

private:
  void PrepareFramebuffers();
  
  void PrepareSceneResource();
  
  void PrepareComputeResource();
  void CreatePrimitiveResource();

  struct Vertex
  {
    glm::vec3 Position;
    glm::vec2 UV;
  };

  ImageObject Load2DTextureFromFile(const char* fileName, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


  void RenderHUD(VkCommandBuffer command);
private:
  // 本アプリで使用するレイアウト(ディスクリプタレイアウト/パイプラインレイアウト)を作成.
  void CreateSampleLayouts();

  ImageObject m_depthBuffer;
  std::vector<VkFramebuffer> m_framebuffers;

  struct FrameCommandBuffer
  {
    VkCommandBuffer commandBuffer;
    VkFence fence;
  };
  std::vector<FrameCommandBuffer> m_commandBuffers;

  std::vector<VkDescriptorSet> m_dsDrawTextures[2];
  
  VkDescriptorSet m_dsWriteToTexture;

  std::vector<BufferObject> m_shaderUniforms;
  VkPipeline   m_pipeline;
  VkPipeline   m_compSepiaPipeline;
  VkPipeline   m_compSobelPipeline;

  VkSampler m_texSampler;
  glm::mat4 m_projection;
  ModelData m_quad, m_quad2;
  int m_selectedFilter;

  ImageObject m_destBuffer;
  ImageObject m_sourceBuffer;
  
  BufferObject CreateStorageBuffer(size_t bufferSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
  VkImageMemoryBarrier CreateImageMemoryBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
};