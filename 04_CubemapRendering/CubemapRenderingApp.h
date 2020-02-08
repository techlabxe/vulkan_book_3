#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include <array>
#include "Camera.h"

class CubemapRenderingApp : public VulkanAppBase
{
public:
  CubemapRenderingApp();

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
 
  VkPipeline CreateRenderTeapotPipeline(
    const std::string& renderPass,
    uint32_t width, uint32_t height,
    const std::string& layoutName,
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages);

  ImageObject LoadCubeTextureFromFile(const char* faceFiles[6]);

  void PrepareRenderTargetForMultiPass();
  void PrepareRenderTargetForSinglePass();

  void PrepareCenterTeapotDescriptors();
  void PrepareAroundTeapotDescriptors();

  void RenderCubemapFaces(VkCommandBuffer command);
  void RenderCubemapOnce(VkCommandBuffer command);
  void RenderToMain(VkCommandBuffer command);
  void RenderHUD(VkCommandBuffer command);

  // リソースバリアの設定.
  void BarrierRTToTexture(VkCommandBuffer command);
  void BarrierTextureToRT(VkCommandBuffer command);

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
  ModelData m_teapot;
  ImageObject m_staticCubemap;
  ImageObject m_cubemapRendered;
  VkSampler m_cubemapSampler;

  struct ShaderParameters
  {
    glm::mat4 world;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightDir;
    glm::vec4 cameraPos;
  };

  struct TeapotInstanceParameters {
    glm::mat4 world[6];
    glm::vec4 colors[6];
  };
  struct ViewProjMatrices
  {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightDir;
  };
  struct MultiViewProjMatrices
  {
    glm::mat4 view[6];
    glm::mat4 proj;
    glm::vec4 lightDir;
  };
  BufferObject m_cubemapEnvUniform;


  // 周辺ティーポット:(To Main)
  struct AroundTeapotsToMainScene
  {
    VkPipeline pipeline;
    std::vector<BufferObject> cameraViewUniform;
    std::vector<VkDescriptorSet> descriptors;

  } m_aroundTeapotsToMain;
  // 周辺ティーポット:(To CubemapFace)
  struct AroundTeapotsToCubeFaceScene
  {
    VkPipeline pipeline;
    std::vector<BufferObject> cameraViewUniform[6];
    std::vector<VkDescriptorSet> descriptors[6];
  } m_aroundTeapotsToFace;

  // 周辺ティーポット:(To CubemapOnce)
  struct AroundTeapotsToCubeScene
  {
    VkPipeline pipeline;
    std::vector<BufferObject> cameraViewUniform;
    std::vector<VkDescriptorSet> descriptors;
  } m_aroundTeapotsToCubemap;

  // 中心のティーポット.
  struct CenterTeapot
  {
    std::vector<VkDescriptorSet> dsCubemapStatic;
    std::vector<VkDescriptorSet> dsCubemapRendered;
    std::vector<BufferObject> sceneUBO;
    VkPipeline pipeline;
  } m_centerTeapot;

  struct CubeFaceScene
  {
    VkImageView viewFaces[6];
    ImageObject depth;
    VkFramebuffer fbFaces[6];
    VkRenderPass  renderPass;
  } m_cubeFaceScene;

  struct CubemapSingleScene
  {
    VkImageView view;
    ImageObject depth;
    VkFramebuffer framebuffer;
    VkRenderPass renderPass;
  } m_cubeScene;


  const uint32_t CubeEdge = 512;
  const VkFormat CubemapFormat = VK_FORMAT_R8G8B8A8_UNORM;
  glm::mat4 m_projection;

  enum Mode {
    Mode_StaticCubemap,
    Mode_MultiPassCubemap,
    Mode_SinglePassCubemap,
  };
  Mode m_mode;
};