#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include <array>
#include "Camera.h"

class TessellateTeapotApp : public VulkanAppBase
{
public:
  TessellateTeapotApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);
  virtual bool OnMouseButtonDown(int button);
  virtual bool OnMouseButtonUp(int button);
  virtual bool OnMouseMove(int dx, int dy);

private:
  // 本アプリで使用するレイアウト(ディスクリプタレイアウト/パイプラインレイアウト)を作成.
  void CreateSampleLayouts();

  void PrepareFramebuffers();
  
  void PrepareSceneResource();

  void PrepareTessTeapot();

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

  uint32_t m_imageIndex;

  Camera m_camera;

  glm::mat4 m_projection;

  struct TessellationShaderParameters
  {
    glm::mat4 world;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightPos;
    glm::vec4 cameraPos;
    float     tessOuterLevel;
    float     tessInnerLevel;
  };

  std::vector<BufferObject> m_tessTeapotUniform;
  std::vector<VkDescriptorSet> m_dsTeapot;
  VkPipeline m_tessTeapotPipeline;
  ModelData m_tessTeapot;

  float m_tessFactor;
};