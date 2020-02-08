#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include "Camera.h"

class HelloGeometryShaderApp : public VulkanAppBase
{
public:
  HelloGeometryShaderApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);
  virtual bool OnMouseButtonDown(int button);
  virtual bool OnMouseButtonUp(int button);
  virtual bool OnMouseMove(int dx, int dy);

  struct ShaderParameters
  {
    glm::mat4 world;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightDir;
  };

private:
  // 本アプリで使用するレイアウト(ディスクリプタレイアウト/パイプラインレイアウト)を作成.
  void CreateSampleLayouts();

  void PrepareFramebuffers();
  void PrepareTeapot();
  void CreatePipeline();

  void RenderHUD(VkCommandBuffer command);
private:
  ImageObject m_depthBuffer;

  std::vector<VkFramebuffer> m_framebuffers;

  struct FrameCommandBuffer
  {
    VkCommandBuffer commandBuffer;
    VkFence fence;
  };
  std::vector<FrameCommandBuffer> m_commandBuffers;

  std::vector<VkDescriptorSet> m_descriptorSets;
  
  std::unordered_map<std::string, VkPipeline> m_pipelines;

  Camera m_camera;
  ModelData m_teapot;
  std::vector<BufferObject> m_uniformBuffers;

  const std::string FlatShadePipeine = "flatShade";
  const std::string SmoothShadePipeline = "smoothShade";
  const std::string NormalVectorPipeline = "drawNormalVector";

  enum DrawMode
  {
    DrawMode_Flat,
    DrawMode_NormalVector,
  };
  DrawMode m_mode;
};