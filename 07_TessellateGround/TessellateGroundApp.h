#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include <array>
#include "Camera.h"

class TessellateGroundApp : public VulkanAppBase
{
public:
  TessellateGroundApp();

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
    glm::vec4 lightPos;
    glm::vec4 cameraPos;
  };

private:
  // 本アプリで使用するレイアウト(ディスクリプタレイアウト/パイプラインレイアウト)を作成.
  void CreateSampleLayouts();

  void PrepareFramebuffers();
  
  void PrepareSceneResource();

  struct Vertex
  {
    glm::vec3 Position;
    glm::vec2 UV;
  };

  ImageObject Load2DTextureFromFile(const char* fileName);
  ImageObject LoadCubeTextureFromFile(const char* faceFiles[6]);

  void PreparePrimitiveResource();

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

  Camera m_camera;
  VkSampler m_texSampler;

  glm::mat4 m_projection;

  struct TessellationShaderParameters
  {
    glm::mat4 world;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightPos;
    glm::vec4 cameraPos;
  };
  ModelData m_quad;
  ImageObject m_heightMap;
  ImageObject m_normalMap;

  std::vector<BufferObject> m_tessUniform;
  std::vector<VkDescriptorSet> m_dsTessSample;
  VkPipeline m_tessGroundPipeline;
  VkPipeline m_tessGroundWired;

  bool m_isWireframe;
};