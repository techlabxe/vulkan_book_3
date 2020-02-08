#include "CubemapRenderingApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"
#include "stb_image.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <array>

using namespace std;

CubemapRenderingApp::CubemapRenderingApp()
{
  m_camera.SetLookAt(
    glm::vec3(0.0f, 2.0f, 10.0f),
    glm::vec3(0.0f, 0.0f, 0.0f)
  );
  m_mode = Mode_StaticCubemap;
}

void CubemapRenderingApp::Prepare()
{
  CreateSampleLayouts();

  auto colorFormat = m_swapchain->GetSurfaceFormat().format;
  RegisterRenderPass("default", CreateRenderPass(colorFormat, VK_FORMAT_D32_SFLOAT));
  RegisterRenderPass("cubemap", CreateRenderPass(CubemapFormat, VK_FORMAT_D32_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
  
  // デプスバッファを準備する.
  auto extent = m_swapchain->GetSurfaceExtent();
  m_depthBuffer = CreateTexture(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

  // フレームバッファの準備.
  PrepareFramebuffers();
  auto imageCount = m_swapchain->GetImageCount();

  // コマンドバッファの準備.
  m_commandBuffers.resize(imageCount);
  for (auto& c : m_commandBuffers)
  {
    c.fence = CreateFence();
    c.commandBuffer = CreateCommandBuffer(false); // コマンドバッファ開始状態にしない.
  }

  PrepareSceneResource();

  // 描画ターゲットの準備.
  PrepareRenderTargetForMultiPass();
  PrepareRenderTargetForSinglePass();

  PrepareCenterTeapotDescriptors();
  PrepareAroundTeapotDescriptors();

  // ティーポットのジオメトリをロード.
  std::vector<TeapotModel::Vertex> vertices(std::begin(TeapotModel::TeapotVerticesPN), std::end(TeapotModel::TeapotVerticesPN));
  std::vector<uint32_t> indices(std::begin(TeapotModel::TeapotIndices), std::end(TeapotModel::TeapotIndices));
  m_teapot = CreateSimpleModel(vertices, indices);

}

void CubemapRenderingApp::Cleanup()
{
  DestroyBuffer(m_teapot.resVertexBuffer);
  DestroyBuffer(m_teapot.resIndexBuffer);

  // AroundTeapots(Main)
  {
    vkDestroyPipeline(m_device, m_aroundTeapotsToMain.pipeline, nullptr);
    for (auto bufferObj : m_aroundTeapotsToMain.cameraViewUniform) DestroyBuffer(bufferObj);
  }

  // AroundTeapots(Face)
  {
    vkDestroyPipeline(m_device, m_aroundTeapotsToFace.pipeline, nullptr);
    for (int face = 0; face < 6; ++face)
    {
      for (auto bufferObj : m_aroundTeapotsToFace.cameraViewUniform[face]) DestroyBuffer(bufferObj);
    }
  }
  // AroundTeapots(Cube)
  {
    vkDestroyPipeline(m_device, m_aroundTeapotsToCubemap.pipeline, nullptr);
    for (auto bufferObj : m_aroundTeapotsToCubemap.cameraViewUniform) DestroyBuffer(bufferObj);
  }
  // CenterTeapot
  {
    vkDestroyPipeline(m_device, m_centerTeapot.pipeline, nullptr);
    for (auto bufferObj : m_centerTeapot.sceneUBO) DestroyBuffer(bufferObj);
  }

  // CubeFaceScene
  {    
    for (auto view : m_cubeFaceScene.viewFaces) vkDestroyImageView(m_device, view, nullptr);
    DestroyImage(m_cubeFaceScene.depth);
    DestroyFramebuffers(_countof(m_cubeFaceScene.fbFaces), m_cubeFaceScene.fbFaces);
  }

  // CubeScene
  {
    vkDestroyImageView(m_device, m_cubeScene.view, nullptr);
    DestroyImage(m_cubeScene.depth);
    DestroyFramebuffers(1, &m_cubeScene.framebuffer);
  }

  DestroyBuffer(m_cubemapEnvUniform);
  DestroyImage(m_cubemapRendered);
  DestroyImage(m_staticCubemap);
  vkDestroySampler(m_device, m_cubemapSampler, nullptr);

  DestroyImage(m_depthBuffer);
  auto count = uint32_t(m_framebuffers.size());
  DestroyFramebuffers(count, m_framebuffers.data());

  for (auto& c : m_commandBuffers)
  {
    DestroyCommandBuffer(c.commandBuffer);
    DestroyFence(c.fence);
  }
  m_commandBuffers.clear();
}

bool CubemapRenderingApp::OnMouseButtonDown(int msg)
{
  if (VulkanAppBase::OnMouseButtonDown(msg))
    return true;

  m_camera.OnMouseButtonDown(msg);
  return true;
}

bool CubemapRenderingApp::OnMouseMove(int dx, int dy)
{
  if (VulkanAppBase::OnMouseMove(dx, dy))
    return true;
  m_camera.OnMouseMove(dx, dy);
  return true;
}

bool CubemapRenderingApp::OnMouseButtonUp(int msg)
{
  if (VulkanAppBase::OnMouseButtonUp(msg))
    return true;
  m_camera.OnMouseButtonUp();
  return true;
}

void CubemapRenderingApp::CreateSampleLayouts()
{
  // ディスクリプタセットレイアウトの準備.
  std::vector<VkDescriptorSetLayoutBinding > dsLayoutBindings;

  // 0: uniformBuffer, 1: texture(+sampler) を使用するシェーダー用レイアウト.
  dsLayoutBindings = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, },
    { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
  };
  VkDescriptorSetLayoutCreateInfo dsLayoutCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr, 0,
    uint32_t(dsLayoutBindings.size()), dsLayoutBindings.data(),
  };

  VkResult result;
  VkDescriptorSetLayout dsLayout;

  result = vkCreateDescriptorSetLayout(m_device, &dsLayoutCI, nullptr, &dsLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
  RegisterLayout("u1t1", dsLayout);

  // 0: uniformBuffer, 1: uniformBuffer を使用するシェーダー用レイアウト.
  dsLayoutBindings = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, },
    { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL },
  };
  dsLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
  dsLayoutCI.pBindings = dsLayoutBindings.data();
  result = vkCreateDescriptorSetLayout(m_device, &dsLayoutCI, nullptr, &dsLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
  RegisterLayout("u2", dsLayout);

  // パイプラインレイアウトの準備
  VkPipelineLayoutCreateInfo layoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0,
  };
  VkPipelineLayout layout;
  dsLayout = GetDescriptorSetLayout("u1t1");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("u1t1", layout);

  dsLayout = GetDescriptorSetLayout("u2");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("u2", layout);

}


void CubemapRenderingApp::Render()
{
  if (m_isMinimizedWindow)
  {
    MsgLoopMinimizedWindow();
  }
  auto result = m_swapchain->AcquireNextImage(&m_imageIndex, m_presentCompletedSem);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    return;
  }

  // Update Uniform Buffer(s)
  {
    auto extent = m_swapchain->GetSurfaceExtent();
    m_projection = glm::perspectiveRH(
      glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );

    ShaderParameters shaderParams{};
    shaderParams.world = glm::mat4(1.0f);

    shaderParams.view = m_camera.GetViewMatrix();
    shaderParams.proj = m_projection;
    shaderParams.lightDir = glm::vec4(0.0f, 10.0f, 10.0f, 0.0f);
    shaderParams.cameraPos = glm::vec4(m_camera.GetPosition(), 1);

    auto ubo = m_centerTeapot.sceneUBO[m_imageIndex];
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &shaderParams, sizeof(ShaderParameters));
    vkUnmapMemory(m_device, ubo.memory);

    auto eye = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 dir[] = {
      glm::vec3(1.0f, 0.0f,0.0f),
      glm::vec3(-1.0f, 0.0f,0.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, 0.0f,-1.0f),
    };
    glm::vec3 up[] = {
      glm::vec3(0.0f,-1.0f, 0.0f),
      glm::vec3(0.0f,-1.0f, 0.0f),
      glm::vec3(0.0f,0.0f, 1.0f),
      glm::vec3(0.0f,0.0f,-1.0f),
      glm::vec3(0.0f,-1.0f, 0.0f),
      glm::vec3(0.0f,-1.0f, 0.0f),
    };

    for (int i = 0; i < 6; ++i)
    {
      ViewProjMatrices matrices;
      matrices.view = glm::lookAt(eye, dir[i], up[i]);
      matrices.proj = glm::perspectiveFovRH(
        glm::radians(45.0f), float(CubeEdge), float(CubeEdge), 0.1f, 100.f);
      matrices.lightDir = shaderParams.lightDir;

      WriteToHostVisibleMemory(m_aroundTeapotsToFace.cameraViewUniform[i][m_imageIndex].memory, sizeof(matrices), &matrices);
    }

    {
      ViewProjMatrices view;
      view.view = m_camera.GetViewMatrix();
      view.proj = m_projection;
      view.lightDir = shaderParams.lightDir;
      WriteToHostVisibleMemory(m_aroundTeapotsToMain.cameraViewUniform[m_imageIndex].memory, sizeof(view), &view);

      MultiViewProjMatrices allViews;
      for (int face = 0; face < 6; ++face)
      {
        allViews.view[face] = glm::lookAt(eye, dir[face], up[face]);
      }
      allViews.proj = glm::perspectiveFovRH(
        glm::radians(45.0f), float(CubeEdge), float(CubeEdge), 0.1f, 100.f);
      allViews.lightDir = shaderParams.lightDir;
      WriteToHostVisibleMemory(m_aroundTeapotsToCubemap.cameraViewUniform[m_imageIndex].memory, sizeof(allViews), &allViews);
    }
  }


  array<VkClearValue, 2> clearValue = {
    {
      { 0.85f, 0.5f, 0.5f, 0.0f}, // for Color
      { 1.0f, 0 }, // for Depth
    }
  };

  auto renderArea = VkRect2D{
    VkOffset2D{0,0},
    m_swapchain->GetSurfaceExtent(),
  };

  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    nullptr,
    GetRenderPass("default"),
    m_framebuffers[m_imageIndex],
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  VkCommandBufferBeginInfo commandBI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr, 0, nullptr
  };

  auto fence = m_commandBuffers[m_imageIndex].fence;
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

  auto command = m_commandBuffers[m_imageIndex].commandBuffer;

  vkBeginCommandBuffer(command, &commandBI);

  if (m_mode != Mode_StaticCubemap)
  {
    switch (m_mode)
    {
    case Mode_MultiPassCubemap:
      RenderCubemapFaces(command);
      break;
    case Mode_SinglePassCubemap:
      RenderCubemapOnce(command);
      break;
    }

  }
  // 描画した内容をテクスチャとして使うためのバリアを設定.
  BarrierRTToTexture(command);

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);
 
  // メイン描画.
  RenderToMain(command);

  // HUD 部分を描画.
  RenderHUD(command);

  vkCmdEndRenderPass(command);

  // 次回の描画に備えてバリアを設定.
  BarrierTextureToRT(command);

  vkEndCommandBuffer(command);

  VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo{
    VK_STRUCTURE_TYPE_SUBMIT_INFO,
    nullptr,
    1, &m_presentCompletedSem, // WaitSemaphore
    &waitStageMask, // DstStageMask
    1, &command, // CommandBuffer
    1, &m_renderCompletedSem, // SignalSemaphore
  };
  vkResetFences(m_device, 1, &fence);
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);

  m_swapchain->QueuePresent(m_deviceQueue, m_imageIndex, m_renderCompletedSem);
}

void CubemapRenderingApp::PrepareFramebuffers()
{
  auto imageCount = m_swapchain->GetImageCount();
  auto extent = m_swapchain->GetSurfaceExtent();
  m_framebuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    vector<VkImageView> views;
    views.push_back(m_swapchain->GetImageView(i));
    views.push_back(m_depthBuffer.view);

    m_framebuffers[i] = CreateFramebuffer(
      GetRenderPass("default"),
      extent.width, extent.height,
      uint32_t(views.size()), views.data()
    );
  }
}

bool CubemapRenderingApp::OnSizeChanged(uint32_t width, uint32_t height)
{
  auto result = VulkanAppBase::OnSizeChanged(width, height);
  if (result)
  {
    DestroyImage(m_depthBuffer);
    DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

    // デプスバッファを再生成.
    auto extent = m_swapchain->GetSurfaceExtent();
    m_depthBuffer = CreateTexture(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    // フレームバッファを準備.
    PrepareFramebuffers();
  }
  return result;
}

void CubemapRenderingApp::PrepareSceneResource()
{
  // 静的なキューブマップの準備.
  const char* files[6] = {
    "posx.jpg", "negx.jpg",
    "posy.jpg", "negy.jpg",
    "posz.jpg", "negz.jpg"
  };
  m_staticCubemap = LoadCubeTextureFromFile(files);
  
  // 描画先となる Cubemap の準備
  VkImageCreateInfo imageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr,
      VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
      VK_IMAGE_TYPE_2D,
      VK_FORMAT_R8G8B8A8_UNORM,
      { CubeEdge, CubeEdge, 1 },
      1, // mipLevels
      6, // arrayLayers
      VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0, nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED
  };

  VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  auto result = vkCreateImage(m_device, &imageCI, nullptr, &m_cubemapRendered.image);
  ThrowIfFailed(result, "vkCreateImage Failed.");
  m_cubemapRendered.memory = AllocateMemory(m_cubemapRendered.image, memProps);
  vkBindImageMemory(m_device, m_cubemapRendered.image, m_cubemapRendered.memory, 0);

  // このキューブマップのアクセスためのビューを準備.
  auto format = VK_FORMAT_R8G8B8A8_UNORM;
  VkImageViewCreateInfo viewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    nullptr, 0,
    m_cubemapRendered.image,
    VK_IMAGE_VIEW_TYPE_CUBE,
    format,
    { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A },
    {
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
    }
  };
  result = vkCreateImageView(m_device, &viewCI, nullptr, &m_cubemapRendered.view);
  ThrowIfFailed(result, "vkCreateImageView Failed.");

  // 周辺オブジェクトの配置用のユニフォームバッファの準備.
  // 位置やカラーの変更をしないため、バッファリングをしない.
  VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  auto bufferSize = uint32_t(sizeof(TeapotInstanceParameters));
  m_cubemapEnvUniform = CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);
  { // 書き込み.
    TeapotInstanceParameters params{};
    params.world[0] = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    params.world[1] = glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 0.0f, 0.0f));
    params.world[2] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f));
    params.world[3] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -5.0f, 0.0f));
    params.world[4] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f));
    params.world[5] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f));
    params.colors[0] = glm::vec4(0.6f, 1.0f, 0.6f, 1.0f);
    params.colors[1] = glm::vec4(0.0f, 0.75f, 1.0f, 1.0f);
    params.colors[2] = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);
    params.colors[3] = glm::vec4(0.5f, 0.5f, 0.25f, 1.0f);
    params.colors[4] = glm::vec4(1.0f, 0.1f, 0.6f, 1.0f);
    params.colors[5] = glm::vec4(1.0f, 0.55f, 0.0f, 1.0f);

    WriteToHostVisibleMemory(m_cubemapEnvUniform.memory, sizeof(params), &params);
  }

  // サンプラーの準備.
  VkSamplerCreateInfo samplerCI{
    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr,
    0,
    VK_FILTER_LINEAR,
    VK_FILTER_LINEAR,
    VK_SAMPLER_MIPMAP_MODE_LINEAR,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    0.0f,
    VK_FALSE,
    1.0f,
    VK_FALSE,
    VK_COMPARE_OP_NEVER,
    0.0f,
    1.0f,
    VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    VK_FALSE
  };
  result = vkCreateSampler(m_device, &samplerCI, nullptr, &m_cubemapSampler);
  ThrowIfFailed(result, "vkCreateSampler failed.");

  auto command = CreateCommandBuffer();
  VkImageMemoryBarrier imageBarrier{
          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          nullptr,
          VK_ACCESS_SHADER_READ_BIT, // srcAccessMask
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // dstAccessMask
          VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_QUEUE_FAMILY_IGNORED,
          VK_QUEUE_FAMILY_IGNORED,
          m_cubemapRendered.image,
          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
  };
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0, nullptr, // memoryBarrier
    0, nullptr, // bufferMemoryBarrier
    1, &imageBarrier
  );

  FinishCommandBuffer(command);
  DestroyCommandBuffer(command);
}

CubemapRenderingApp::ImageObject CubemapRenderingApp::LoadCubeTextureFromFile(const char* faceFiles[6])
{
  int width, height;
  stbi_uc* faceImages[6] = { 0 };
  for (int i = 0; i < 6; ++i)
  {
    faceImages[i] = stbi_load(faceFiles[i], &width, &height, nullptr, 4);
  }

  VkImageCreateInfo imageCI{
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr,
    VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, // Cubemap として使うため.
    VK_IMAGE_TYPE_2D,
    VK_FORMAT_R8G8B8A8_UNORM, { uint32_t(width), uint32_t(height), 1u },
    1,
    6,
    VK_SAMPLE_COUNT_1_BIT,
    VK_IMAGE_TILING_OPTIMAL,
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    VK_SHARING_MODE_EXCLUSIVE,
    0, nullptr,
    VK_IMAGE_LAYOUT_UNDEFINED
  };
  VkImage cubemapImage;
  VkDeviceMemory cubemapMemory;
  auto result = vkCreateImage(m_device, &imageCI, nullptr, &cubemapImage);
  VkMemoryRequirements reqs;
  vkGetImageMemoryRequirements(m_device, cubemapImage, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  vkAllocateMemory(m_device, &info, nullptr, &cubemapMemory);
  vkBindImageMemory(m_device, cubemapImage, cubemapMemory, 0);

  VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageViewCreateInfo viewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    nullptr, 0,
    cubemapImage,
    VK_IMAGE_VIEW_TYPE_CUBE, imageCI.format,
    book_util::DefaultComponentMapping(),
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6}
  };
  VkImageView cubemapView;
  result = vkCreateImageView(m_device, &viewCI, nullptr, &cubemapView);

  // ステージング用準備
  auto bufferSize = uint32_t(width * height * sizeof(uint32_t));
  BufferObject buffersSrc[6];
  for (int i = 0; i < 6; ++i)
  {
    buffersSrc[i] = CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    WriteToHostVisibleMemory(buffersSrc[i].memory, bufferSize, faceImages[i]);
  }
  // 転送.
  auto command = CreateCommandBuffer();

  VkImageSubresourceRange subresource{};
  subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  subresource.baseMipLevel = 0;
  subresource.levelCount = 1;
  subresource.baseArrayLayer = 0;
  subresource.layerCount = 6;

  VkImageMemoryBarrier imb{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
    0, VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
    cubemapImage,
    subresource
  };
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr,
    0, nullptr,
    1, &imb);

  for (int i = 0; i < 6; ++i)
  {
    VkBufferImageCopy  region{};
    region.imageExtent = { uint32_t(width), uint32_t(height), 1 };
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageSubresource.baseArrayLayer = i;

    vkCmdCopyBufferToImage(
      command,
      buffersSrc[i].buffer,
      cubemapImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1, &region
    );
  }

  imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(
    command,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0, 0, nullptr,
    0, nullptr,
    1, &imb);

  FinishCommandBuffer(command);
  DestroyCommandBuffer(command);

  for (int i = 0; i < 6; ++i)
  {
    stbi_image_free(faceImages[i]);
    vkDestroyBuffer(m_device, buffersSrc[i].buffer, nullptr);
    vkFreeMemory(m_device, buffersSrc[i].memory, nullptr);
  }

  ImageObject cubemap;
  cubemap.image = cubemapImage;
  cubemap.memory = cubemapMemory;
  cubemap.view = cubemapView;
  return cubemap;
}

VkPipeline CubemapRenderingApp::CreateRenderTeapotPipeline(
  const std::string& renderPass,
  uint32_t width, uint32_t height,
  const std::string& layoutName,
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages)
{
  // パイプラインを準備.
  auto stride = uint32_t(sizeof(TeapotModel::Vertex));
  VkVertexInputBindingDescription vibDesc{
      0, // binding
      stride,
      VK_VERTEX_INPUT_RATE_VERTEX
  };

  array<VkVertexInputAttributeDescription, 2> inputAttribs{
    {
      { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TeapotModel::Vertex, Position) },
      { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TeapotModel::Vertex, Normal) },
    }
  };
  VkPipelineVertexInputStateCreateInfo pipelineVisCI{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &vibDesc,
    uint32_t(inputAttribs.size()), inputAttribs.data()
  };

  auto blendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();
  VkPipelineColorBlendStateCreateInfo colorBlendStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    nullptr, 0,
    VK_FALSE, VK_LOGIC_OP_CLEAR, // logicOpEnable
    1, &blendAttachmentState,
    { 0.0f, 0.0f, 0.0f,0.0f }
  };
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_FALSE,
  };
  VkPipelineMultisampleStateCreateInfo multisampleCI{
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    nullptr, 0,
    VK_SAMPLE_COUNT_1_BIT,
    VK_FALSE, // sampleShadingEnable
    0.0f, nullptr,
    VK_FALSE, VK_FALSE,
  };
  auto viewport = book_util::GetViewportFlipped(float(width), float(height));
  auto scissor = VkRect2D{ { 0, 0}, {width, height} };

  auto viewportStateCI = VkPipelineViewportStateCreateInfo{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
  };
  viewportStateCI.viewportCount = 1;
  viewportStateCI.pViewports = &viewport;
  viewportStateCI.scissorCount = 1;
  viewportStateCI.pScissors = &scissor;

  auto rasterizerState = book_util::GetDefaultRasterizerState();
  auto dsState = book_util::GetDefaultDepthStencilState();

  // DynamicState
  vector<VkDynamicState> dynamicStates{
    VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT
  };
  VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
    uint32_t(dynamicStates.size()), dynamicStates.data(),
  };

  VkGraphicsPipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0,
  };
  pipelineCI.pVertexInputState = &pipelineVisCI;
  pipelineCI.pInputAssemblyState = &inputAssemblyCI;
  pipelineCI.pRasterizationState = &rasterizerState;
  pipelineCI.pMultisampleState = &multisampleCI;
  pipelineCI.pDepthStencilState = &dsState;
  pipelineCI.pColorBlendState = &colorBlendStateCI;
  pipelineCI.pViewportState = &viewportStateCI;
  pipelineCI.pDynamicState = &pipelineDynamicStateCI;
  pipelineCI.pStages = shaderStages.data();
  pipelineCI.stageCount = uint32_t(shaderStages.size());
  pipelineCI.renderPass = GetRenderPass(renderPass);
  pipelineCI.layout = GetPipelineLayout(layoutName);

  VkPipeline pipeline;
  auto result = vkCreateGraphicsPipelines(
    m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline failed.");
  return pipeline;
}


void CubemapRenderingApp::PrepareCenterTeapotDescriptors()
{
  auto dsLayout = GetDescriptorSetLayout("u1t1");
  auto imageCount = m_swapchain->GetImageCount();

  auto bufferSize = uint32_t(sizeof(ShaderParameters));
  m_centerTeapot.sceneUBO = CreateUniformBuffers(bufferSize, imageCount);

  // ファイルから読み込んだキューブマップを使用して描画するパスのディスクリプタを準備.
  m_centerTeapot.dsCubemapStatic.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    auto ds = AllocateDescriptorSet(dsLayout);
    m_centerTeapot.dsCubemapStatic[i] = ds;

    VkDescriptorBufferInfo sceneUbo{
      m_centerTeapot.sceneUBO[i].buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorImageInfo  staticCubemap{
      m_cubemapSampler, m_staticCubemap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    std::vector<VkWriteDescriptorSet> writeSet = {
      book_util::CreateWriteDescriptorSet(ds, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &sceneUbo),
      book_util::CreateWriteDescriptorSet(ds, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &staticCubemap),
    };
    vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
  }

  // 動的に描画したキューブマップを使用して描画するパスのディスクリプタを準備.
  m_centerTeapot.dsCubemapRendered.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    auto ds = AllocateDescriptorSet(dsLayout);
    m_centerTeapot.dsCubemapRendered[i] = ds;

    VkDescriptorBufferInfo sceneUbo{
      m_centerTeapot.sceneUBO[i].buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorImageInfo renderedCubemap{
      m_cubemapSampler, m_cubemapRendered.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    std::vector<VkWriteDescriptorSet> writeSet = {
      book_util::CreateWriteDescriptorSet(ds, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &sceneUbo),
      book_util::CreateWriteDescriptorSet(ds, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &renderedCubemap),
    };
    vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
  }

  std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
    book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };
  auto extent = m_swapchain->GetSurfaceExtent();
  auto renderPass = GetRenderPass("default");
  auto layout = GetPipelineLayout("u1t1");
  m_centerTeapot.pipeline = CreateRenderTeapotPipeline(
    "default",
    extent.width, extent.height,
    "u1t1",
    shaderStages
  );
  book_util::DestroyShaderModules(m_device, shaderStages);
}

void CubemapRenderingApp::PrepareAroundTeapotDescriptors()
{
  auto dsLayout = GetDescriptorSetLayout("u2");
  auto imageCount = m_swapchain->GetImageCount();

  auto bufferSize = uint32_t(sizeof(ViewProjMatrices));
  for (int face = 0; face < 6; ++face)
  {
    m_aroundTeapotsToFace.cameraViewUniform[face] = CreateUniformBuffers(bufferSize, imageCount);

  }
  m_aroundTeapotsToMain.cameraViewUniform = CreateUniformBuffers(bufferSize, imageCount);
  bufferSize = uint32_t(sizeof(MultiViewProjMatrices));
  m_aroundTeapotsToCubemap.cameraViewUniform = CreateUniformBuffers(bufferSize, imageCount);

  for (int face = 0; face < 6; ++face)
  {
    m_aroundTeapotsToFace.descriptors[face].resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
      // キューブマップへ描画するパスのディスクリプタを準備.
      auto ds = AllocateDescriptorSet(dsLayout);
      m_aroundTeapotsToFace.descriptors[face][i] = ds;

      VkDescriptorBufferInfo instanceUbo{
        m_cubemapEnvUniform.buffer, 0, VK_WHOLE_SIZE
      };
      VkDescriptorBufferInfo viewProjParamUbo{
        m_aroundTeapotsToFace.cameraViewUniform[face][i].buffer, 0, VK_WHOLE_SIZE
      };
      std::vector<VkWriteDescriptorSet> writeSet = {
        book_util::CreateWriteDescriptorSet(ds, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &instanceUbo),
        book_util::CreateWriteDescriptorSet(ds, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &viewProjParamUbo),
      };
      vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
    }
  }
  
  // シングルパスのディスクリプタを準備.
  m_aroundTeapotsToCubemap.descriptors.resize(imageCount);
  m_aroundTeapotsToCubemap.cameraViewUniform.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    auto ds = AllocateDescriptorSet(dsLayout);
    m_aroundTeapotsToCubemap.descriptors[i] = ds;

    VkDescriptorBufferInfo instanceUbo{
      m_cubemapEnvUniform.buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo viewProjParamUbo{
      m_aroundTeapotsToCubemap.cameraViewUniform[i].buffer, 0, VK_WHOLE_SIZE
    };
    std::vector<VkWriteDescriptorSet> writeSet = {
      book_util::CreateWriteDescriptorSet(ds, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &instanceUbo),
      book_util::CreateWriteDescriptorSet(ds, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &viewProjParamUbo),
    };
    vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
  }

  // メインの描画パスで描画するためのディスクリプタを準備.
  m_aroundTeapotsToMain.cameraViewUniform.resize(imageCount);
  m_aroundTeapotsToMain.descriptors.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    auto ds = AllocateDescriptorSet(dsLayout);
    m_aroundTeapotsToMain.descriptors[i] = ds;

    VkDescriptorBufferInfo instanceUbo{
      m_cubemapEnvUniform.buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo viewProjParamUbo{
      m_aroundTeapotsToMain.cameraViewUniform[i].buffer, 0, VK_WHOLE_SIZE
    };
    std::vector<VkWriteDescriptorSet> writeSet = {
      book_util::CreateWriteDescriptorSet(ds, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &instanceUbo),
      book_util::CreateWriteDescriptorSet(ds, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &viewProjParamUbo),
    };
    vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
  }
  
  
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
  // マルチ描画パス.
  shaderStages = {
    book_util::LoadShader(m_device, "teapotsVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "teapotsFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };
  m_aroundTeapotsToFace.pipeline = CreateRenderTeapotPipeline(
    "cubemap", CubeEdge, CubeEdge, "u2", shaderStages);
  book_util::DestroyShaderModules(m_device, shaderStages);

  // シングル描画パス.
  shaderStages = {
    book_util::LoadShader(m_device, "cubemapVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "cubemapGS.spv", VK_SHADER_STAGE_GEOMETRY_BIT),
    book_util::LoadShader(m_device, "cubemapFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };
  m_aroundTeapotsToCubemap.pipeline = CreateRenderTeapotPipeline(
    "cubemap", CubeEdge, CubeEdge, "u2", shaderStages);
  book_util::DestroyShaderModules(m_device, shaderStages);

  // メイン描画パス.
  auto extent = m_swapchain->GetSurfaceExtent();
  shaderStages = {
    book_util::LoadShader(m_device, "teapotsVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "teapotsFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };
  m_aroundTeapotsToMain.pipeline = CreateRenderTeapotPipeline(
    "default", extent.width, extent.height, "u2", shaderStages);
  book_util::DestroyShaderModules(m_device, shaderStages);
}


void CubemapRenderingApp::PrepareRenderTargetForMultiPass()
{
  VkResult result;
  VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  
  for (int face = 0; face < 6; ++face)
  {
    VkImageViewCreateInfo viewCI{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr, 0,
        m_cubemapRendered.image,
        VK_IMAGE_VIEW_TYPE_2D,
        CubemapFormat,
        { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A },
        {
          VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
          uint32_t(face), // baseArrayLayer
          1  // layerCount
        }
    };
    result = vkCreateImageView(m_device, &viewCI, nullptr, &m_cubeFaceScene.viewFaces[face]);
    ThrowIfFailed(result, "vkCreateImageView Failed.");
  }

  VkImageCreateInfo depthImageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr,
      0,
      VK_IMAGE_TYPE_2D,
      VK_FORMAT_D32_SFLOAT,
      { CubeEdge, CubeEdge, 1 },
      1, 1, VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0, nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED
  };
  result = vkCreateImage(m_device, &depthImageCI, nullptr, &m_cubeFaceScene.depth.image);
  ThrowIfFailed(result, "vkCreateImage failed.");
  m_cubeFaceScene.depth.memory = AllocateMemory(m_cubeFaceScene.depth.image, memProps);
  vkBindImageMemory(m_device, m_cubeFaceScene.depth.image, m_cubeFaceScene.depth.memory, 0);

  VkImageViewCreateInfo depthViewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr,
    0,
    m_cubeFaceScene.depth.image,
    VK_IMAGE_VIEW_TYPE_2D,
    depthImageCI.format,
    { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A },
    { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
  };
  result = vkCreateImageView(m_device, &depthViewCI, nullptr, &m_cubeFaceScene.depth.view);
  ThrowIfFailed(result, "vkCreateImageView failed.");
  
  m_cubeFaceScene.renderPass = GetRenderPass("cubemap");
  
  for (int i = 0; i < 6; ++i)
  {
    std::array<VkImageView, 2> attachments;
    attachments[0] = m_cubeFaceScene.viewFaces[i];
    attachments[1] = m_cubeFaceScene.depth.view;

    VkFramebufferCreateInfo fbCI{
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0,
      m_cubeFaceScene.renderPass,
      uint32_t(attachments.size()), attachments.data(),
      CubeEdge, CubeEdge, 1,
    };
    result = vkCreateFramebuffer(m_device, &fbCI, nullptr, &m_cubeFaceScene.fbFaces[i]);
    ThrowIfFailed(result, "vkCreateFramebuffer failed.");
  }
}

void CubemapRenderingApp::PrepareRenderTargetForSinglePass()
{
  VkResult result;
  VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  
  VkImageViewCreateInfo imageViewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr,
    0,
    m_cubemapRendered.image,
    VK_IMAGE_VIEW_TYPE_2D,
    CubemapFormat,
    { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A },
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
  };
  result = vkCreateImageView(m_device, &imageViewCI, nullptr, &m_cubeScene.view);
  ThrowIfFailed(result, "vkCreateImageView failed.");

  VkImageCreateInfo depthImageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr,
      VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, // Depthも Cubemap サイズ分必要.
      VK_IMAGE_TYPE_2D,
      VK_FORMAT_D32_SFLOAT,
      { CubeEdge, CubeEdge, 1 },
      1,
      6,
      VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0, nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED
  };
  result = vkCreateImage(m_device, &depthImageCI, nullptr, &m_cubeScene.depth.image);
  ThrowIfFailed(result, "vkCreateImage failed.");
  VkMemoryRequirements reqs;
  vkGetImageMemoryRequirements(m_device, m_cubeScene.depth.image, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  vkAllocateMemory(m_device, &info, nullptr, &m_cubeScene.depth.memory);
  vkBindImageMemory(m_device, m_cubeScene.depth.image, m_cubeScene.depth.memory, 0);

  VkImageViewCreateInfo depthViewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr,
    0,
    m_cubeScene.depth.image,
    VK_IMAGE_VIEW_TYPE_2D,
    depthImageCI.format,
    { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A },
    { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6 }
  };
  result = vkCreateImageView(m_device, &depthViewCI, nullptr, &m_cubeScene.depth.view);
  ThrowIfFailed(result, "vkCreateImageView failed.");

  m_cubeScene.renderPass = GetRenderPass("cubemap");

  // 6つの面(VkImageView)へ接続する VkFramebuffer を準備.
  {
    std::array<VkImageView, 2> attachments;
    attachments[0] = m_cubeScene.view;
    attachments[1] = m_cubeScene.depth.view;
    VkFramebufferCreateInfo fbCI{
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr,
      0,
      m_cubeScene.renderPass,
      uint32_t(attachments.size()), attachments.data(),
      CubeEdge, CubeEdge, 6,
    };
    result = vkCreateFramebuffer(m_device, &fbCI, nullptr, &m_cubeScene.framebuffer);
    ThrowIfFailed(result, "vkCreateFramebuffer failed.");
  }
}


void CubemapRenderingApp::RenderCubemapFaces(VkCommandBuffer command)
{
  VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, };
  auto renderArea = VkRect2D{ VkOffset2D{0,0}, VkExtent2D{ CubeEdge, CubeEdge} };
  array<VkClearValue, 2> clearValue = {
   {
     { 0.5f, 0.75f, 1.0f, 0.0f}, // for Color
     { 1.0f, 0 }, // for Depth
   }
  };
  auto extent = VkExtent2D{CubeEdge, CubeEdge};
  VkViewport viewport = {
    0.0f, 0.0f, float(CubeEdge), float(CubeEdge), 0.0f, 1.0f
  };
  VkRect2D scissor{
    { 0, 0}, {CubeEdge, CubeEdge},
  };


  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr,
    m_cubeFaceScene.renderPass,
    VK_NULL_HANDLE,
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };
  for (int face = 0; face < 6; ++face)
  {
    rpBI.framebuffer = m_cubeFaceScene.fbFaces[face];
    vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

    auto pipelineLayout = GetPipelineLayout("u2");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_aroundTeapotsToFace.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_aroundTeapotsToFace.descriptors[face][m_imageIndex], 0, nullptr);

    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdSetViewport(command, 0, 1, &viewport);

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
    vkCmdDrawIndexed(command, m_teapot.indexCount, 6, 0, 0, 0);
    vkCmdEndRenderPass(command);
  }

}

void CubemapRenderingApp::RenderCubemapOnce(VkCommandBuffer command)
{
  VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, };
  auto renderArea = VkRect2D{ VkOffset2D{0,0}, VkExtent2D{ CubeEdge, CubeEdge} };
  array<VkClearValue, 2> clearValue = {
   {
     { 0.5f, 0.75f, 1.0f, 0.0f}, // for Color
     { 1.0f, 0 }, // for Depth
   }
  };

  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr,
    m_cubeFaceScene.renderPass,
    VK_NULL_HANDLE,
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  auto extent = VkExtent2D{ CubeEdge, CubeEdge };
  VkViewport viewport = {
    0.0f, 0.0f, float(CubeEdge), float(CubeEdge), 0.0f, 1.0f
  };
  VkRect2D scissor{
    { 0, 0}, {CubeEdge, CubeEdge},
  };

  rpBI.framebuffer = m_cubeScene.framebuffer;
  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  auto pipelineLayout = GetPipelineLayout("u2");
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_aroundTeapotsToCubemap.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_aroundTeapotsToCubemap.descriptors[m_imageIndex], 0, nullptr);
  
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);
  
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_teapot.indexCount, 6, 0, 0, 0);
  vkCmdEndRenderPass(command);
}


void CubemapRenderingApp::RenderToMain(VkCommandBuffer command)
{
  auto pipelineLayout = GetPipelineLayout("u1t1");
  auto imageIndex = m_imageIndex;
  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0},
    extent
  };

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_centerTeapot.pipeline);

  VkDescriptorSet ds;
  if ( m_mode == Mode_StaticCubemap )
  {
    ds = m_centerTeapot.dsCubemapStatic[imageIndex];
  }
  else
  {
    ds = m_centerTeapot.dsCubemapRendered[imageIndex];
  }
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);

  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);
  vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);

  pipelineLayout = GetPipelineLayout("u2");
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_aroundTeapotsToMain.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_aroundTeapotsToMain.descriptors[imageIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_teapot.indexCount, 6, 0, 0, 0);
}

void CubemapRenderingApp::RenderHUD(VkCommandBuffer command)
{
  // ImGui
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  ImGui::Begin("Information");
  ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
  ImGui::Combo("Mode", (int*)&m_mode, "Static\0MultiPass\0SinglePass\0\0");
  ImGui::End();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(
    ImGui::GetDrawData(), command
  );

}

void CubemapRenderingApp::BarrierRTToTexture(VkCommandBuffer command)
{
  VkImageMemoryBarrier imageBarrier{
          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          nullptr,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // srcAccessMask
          VK_ACCESS_SHADER_READ_BIT, // dstAccessMask
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_QUEUE_FAMILY_IGNORED,
          VK_QUEUE_FAMILY_IGNORED,
          m_cubemapRendered.image,
          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
  };
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0,
    0, nullptr, // memoryBarrier
    0, nullptr, // bufferMemoryBarrier
    1, &imageBarrier
  );
}

void CubemapRenderingApp::BarrierTextureToRT(VkCommandBuffer command)
{
  VkImageMemoryBarrier imageBarrier{
          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          nullptr,
          VK_ACCESS_SHADER_READ_BIT, // srcAccessMask
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // dstAccessMask
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_QUEUE_FAMILY_IGNORED,
          VK_QUEUE_FAMILY_IGNORED,
          m_cubemapRendered.image,
          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
  };
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0, nullptr, // memoryBarrier
    0, nullptr, // bufferMemoryBarrier
    1, &imageBarrier
  );
}
