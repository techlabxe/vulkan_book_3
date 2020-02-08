#include "TessellateGroundApp.h"
#include "VulkanBookUtil.h"


#include <array>

#include <glm/gtc/matrix_transform.hpp>

#include "stb_image.h"

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

using namespace std;

TessellateGroundApp::TessellateGroundApp()
{
  m_camera.SetLookAt(
    glm::vec3(48.5f, 25.0f, 65.0f),
    glm::vec3(0.0f, 0.0f, 0.0f)
  );
  m_isWireframe = true;
}

void TessellateGroundApp::Prepare()
{
  CreateSampleLayouts();

  auto colorFormat = m_swapchain->GetSurfaceFormat().format;
  RegisterRenderPass("default", CreateRenderPass(colorFormat, VK_FORMAT_D32_SFLOAT));

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

  PreparePrimitiveResource();
}

void TessellateGroundApp::Cleanup()
{
  vkDestroyPipeline(m_device, m_tessGroundPipeline, nullptr);
  vkDestroyPipeline(m_device, m_tessGroundWired, nullptr);
  vkDestroySampler(m_device, m_texSampler, nullptr);

  DestroyImage(m_normalMap);
  DestroyImage(m_heightMap);

  for (auto& ubo : m_tessUniform)
  {
    DestroyBuffer(ubo);
  }
  m_tessUniform.clear();

  DestroyBuffer(m_quad.resVertexBuffer);
  DestroyBuffer(m_quad.resIndexBuffer);

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

bool TessellateGroundApp::OnMouseButtonDown(int msg)
{
  if (VulkanAppBase::OnMouseButtonDown(msg))
    return true;

  m_camera.OnMouseButtonDown(msg);
  return true;
}

bool TessellateGroundApp::OnMouseMove(int dx, int dy)
{
  if (VulkanAppBase::OnMouseMove(dx, dy))
    return true;

  m_camera.OnMouseMove(dx, dy);
  return true;
}

bool TessellateGroundApp::OnMouseButtonUp(int msg)
{
  if (VulkanAppBase::OnMouseButtonUp(msg))
    return true;

  m_camera.OnMouseButtonUp();
  return true;
}

void TessellateGroundApp::CreateSampleLayouts()
{
  // ディスクリプタセットレイアウトの準備.
  std::vector<VkDescriptorSetLayoutBinding > dsLayoutBindings;

  // 0: uniformBuffer, 1: texture(+sampler) を使用するシェーダー用レイアウト.
  dsLayoutBindings = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, },
    { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL },
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

  // 0: uniformBuffer, 1,2: texture(+sampler) を使用するシェーダー用レイアウト.
  dsLayoutBindings = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, },
    { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL },
    { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL },
  };
  dsLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
  dsLayoutCI.pBindings = dsLayoutBindings.data();
  result = vkCreateDescriptorSetLayout(m_device, &dsLayoutCI, nullptr, &dsLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
  RegisterLayout("u1t2", dsLayout);

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

  dsLayoutBindings = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, },
  };
  dsLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
  dsLayoutCI.pBindings = dsLayoutBindings.data();
  result = vkCreateDescriptorSetLayout(m_device, &dsLayoutCI, nullptr, &dsLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
  RegisterLayout("u1", dsLayout);

  // パイプラインレイアウトの準備
  VkPipelineLayoutCreateInfo layoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    nullptr, 0,
    0, nullptr, // SetLayouts
    0, nullptr, // PushConstants
  };
  VkPipelineLayout layout;

  dsLayout = GetDescriptorSetLayout("u1t1");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("u1t1", layout);

  dsLayout = GetDescriptorSetLayout("u1t2");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("u1t2", layout);

  dsLayout = GetDescriptorSetLayout("u2");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("u2", layout);

  dsLayout = GetDescriptorSetLayout("u1");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("u1", layout);
}

void TessellateGroundApp::Render()
{
  if (m_isMinimizedWindow)
  {
    MsgLoopMinimizedWindow();
  }
  uint32_t imageIndex = 0;
  auto result = m_swapchain->AcquireNextImage(&imageIndex, m_presentCompletedSem);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    return;
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
    m_framebuffers[imageIndex],
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  VkCommandBufferBeginInfo commandBI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr, 0, nullptr
  };

  {
    auto extent = m_swapchain->GetSurfaceExtent();
    m_projection = glm::perspectiveRH(
      glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );
  }

  {
    TessellationShaderParameters tessParams;
    tessParams.world = glm::mat4(1.0);
    tessParams.view = m_camera.GetViewMatrix();
    tessParams.proj = m_projection;
    tessParams.lightPos = glm::vec4(0.0f);
    tessParams.cameraPos = glm::vec4(m_camera.GetPosition(), 0.0f);
    WriteToHostVisibleMemory(m_tessUniform[imageIndex].memory, sizeof(tessParams), &tessParams);
  }

  auto fence = m_commandBuffers[imageIndex].fence;
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

  auto command = m_commandBuffers[imageIndex].commandBuffer;

  vkBeginCommandBuffer(command, &commandBI);

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);
  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0},
    extent
  };
  VkDeviceSize offsets[] = { 0 };

  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);
 
  auto pipelineLayout = GetPipelineLayout("u1t2");
  if (m_isWireframe)
  {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessGroundWired);
  }
  else
  {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessGroundPipeline);
  }
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_dsTessSample[imageIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_quad.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdBindVertexBuffers(command, 0, 1, &m_quad.resVertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_quad.indexCount, 1, 0, 0, 0);

  RenderHUD(command);

  vkCmdEndRenderPass(command);
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

  m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);
}

void TessellateGroundApp::PrepareFramebuffers()
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

bool TessellateGroundApp::OnSizeChanged(uint32_t width, uint32_t height)
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

void TessellateGroundApp::PrepareSceneResource()
{
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
  vkCreateSampler(m_device, &samplerCI, nullptr, &m_texSampler);

  m_heightMap = Load2DTextureFromFile("heightmap.png");
  m_normalMap = Load2DTextureFromFile("normalmap.png");
}

TessellateGroundApp::ImageObject TessellateGroundApp::Load2DTextureFromFile(const char* fileName)
{
  int width, height;
  stbi_uc* rawimage = nullptr;
  rawimage = stbi_load(fileName, &width, &height, nullptr, 4);

  VkImageCreateInfo imageCI{
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr,
    0,
    VK_IMAGE_TYPE_2D,
    VK_FORMAT_R8G8B8A8_UNORM, { uint32_t(width), uint32_t(height), 1u },
    1, 1,
    VK_SAMPLE_COUNT_1_BIT,
    VK_IMAGE_TILING_OPTIMAL,
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    VK_SHARING_MODE_EXCLUSIVE,
    0, nullptr,
    VK_IMAGE_LAYOUT_UNDEFINED
  };
  VkResult result;
  VkImage image;
  result = vkCreateImage(m_device, &imageCI, nullptr, &image);
  ThrowIfFailed(result, "vkCreateImage failed.");
  VkDeviceMemory memory;
  VkMemoryRequirements reqs;
  vkGetImageMemoryRequirements(m_device, image, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  vkAllocateMemory(m_device, &info, nullptr, &memory);
  vkBindImageMemory(m_device, image, memory, 0);

  VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageViewCreateInfo viewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    nullptr, 0,
    image,
    VK_IMAGE_VIEW_TYPE_2D, imageCI.format,
    book_util::DefaultComponentMapping(),
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
  };
  VkImageView view;
  result = vkCreateImageView(m_device, &viewCI, nullptr, &view);

  // ステージング用準備
  auto bufferSize = uint32_t(width * height * sizeof(uint32_t));
  BufferObject buffersSrc;
  buffersSrc = CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  WriteToHostVisibleMemory(buffersSrc.memory, bufferSize, rawimage);

  // 転送.
  auto command = CreateCommandBuffer();
  VkImageSubresourceRange subresource{};
  subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  subresource.baseMipLevel = 0;
  subresource.levelCount = 1;
  subresource.baseArrayLayer = 0;
  subresource.layerCount = 1;

  VkImageMemoryBarrier imb{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
    0, VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
    image,
    subresource
  };
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr,
    0, nullptr,
    1, &imb);

  VkBufferImageCopy  region{};
  region.imageExtent = { uint32_t(width), uint32_t(height), 1 };
  region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  region.imageSubresource.baseArrayLayer = 0;

  vkCmdCopyBufferToImage(
    command,
    buffersSrc.buffer,
    image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1, &region
  );

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

  stbi_image_free(rawimage);
  DestroyBuffer(buffersSrc);

  ImageObject texture;
  texture.image = image;
  texture.memory = memory;
  texture.view = view;
  return texture;

}

TessellateGroundApp::ImageObject TessellateGroundApp::LoadCubeTextureFromFile(const char* faceFiles[6])
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

void TessellateGroundApp::PreparePrimitiveResource()
{
  using namespace glm;
  using VertexData = std::vector<Vertex>;
  using IndexData = std::vector<uint32_t>;
  const float edge = 200.0f;
  const int divide = 10;

  VertexData vertices;
  for (int z = 0; z < divide + 1; ++z)
  {
    for (int x = 0; x < divide + 1; ++x)
    {
      Vertex v;
      v.Position = vec3(
        edge * x / divide,
        0.0f,
        edge * z / divide
      );
      v.UV = vec2(
        v.Position.x / edge,
        v.Position.z / edge
      );
      vertices.push_back(v);
    }
  }
  IndexData indices;
  for (int z = 0; z < divide; ++z)
  {
    for (int x = 0; x < divide; ++x)
    {
      const int rows = divide + 1;
      int v0 = x, v1 = x + 1;
      v0 = v0 + rows * z;
      v1 = v1 + rows * z;
      indices.push_back(v0);
      indices.push_back(v1);
      indices.push_back(v0 + rows);
      indices.push_back(v1 + rows);
    }
  }
  // 中心補正.
  for (auto& v : vertices)
  {
    v.Position.x -= edge * 0.5f;
    v.Position.z -= edge * 0.5f;
  }
  m_quad = CreateSimpleModel(vertices, indices);

  auto imageCount = int(m_swapchain->GetImageCount());
  m_tessUniform = CreateUniformBuffers(sizeof(TessellationShaderParameters), imageCount);

  VkResult result;
  VkDescriptorSetLayout dsLayout = GetDescriptorSetLayout("u1t2");
  VkDescriptorSetAllocateInfo dsAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &dsLayout
  };
  m_dsTessSample.resize(imageCount);
  for (int i = 0; i < imageCount; ++i)
  {
    result = vkAllocateDescriptorSets(m_device, &dsAI, &m_dsTessSample[i]);
    ThrowIfFailed(result, "vkAllocateDescriptorSets failed.");
  }

  for (int i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo bufferInfo{
      m_tessUniform[i].buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorImageInfo imageInfo{
      m_texSampler,
      m_heightMap.view,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkDescriptorImageInfo imageInfo2{
      m_texSampler,
      m_normalMap.view,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    std::vector<VkWriteDescriptorSet> writeDS = {
      book_util::CreateWriteDescriptorSet(m_dsTessSample[i], 0, &bufferInfo),
      book_util::CreateWriteDescriptorSet(m_dsTessSample[i], 1, &imageInfo),
      book_util::CreateWriteDescriptorSet(m_dsTessSample[i], 2, &imageInfo2)
    };
    vkUpdateDescriptorSets(m_device, uint32_t(writeDS.size()), writeDS.data(), 0, nullptr);
  }

  // パイプラインレイアウトの準備
  VkPipelineLayout layout = GetPipelineLayout("u1t2");

  // パイプライン構築.
  auto stride = uint32_t(sizeof(Vertex));
  VkVertexInputBindingDescription vibDesc{
      0, // binding
      stride,
      VK_VERTEX_INPUT_RATE_VERTEX
  };

  array<VkVertexInputAttributeDescription, 2> inputAttribs{
    {
      { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position) },
      { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, UV) },
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
    nullptr, 0, VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
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

  VkPipelineTessellationStateCreateInfo tessStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO, 
    nullptr,
    0,
    4
  };

  auto extentBackbuffer = m_swapchain->GetSurfaceExtent();
  auto viewportBackbuffer = book_util::GetViewportFlipped(float(extentBackbuffer.width), float(extentBackbuffer.height));
  auto scissorBackbuffer = VkRect2D{
    { 0, 0}, extentBackbuffer
  };

  auto viewportStateCI = VkPipelineViewportStateCreateInfo{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
  };

  auto rasterizerState = book_util::GetDefaultRasterizerState();
  auto dsState = book_util::GetDefaultDepthStencilState();

  rasterizerState.cullMode = VK_CULL_MODE_BACK_BIT;

  // DynamicState
  vector<VkDynamicState> dynamicStates{
    VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT
  };
  VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
    uint32_t(dynamicStates.size()), dynamicStates.data(),
  };

  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
  VkGraphicsPipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0,
  };
  pipelineCI.pVertexInputState = &pipelineVisCI;
  pipelineCI.pInputAssemblyState = &inputAssemblyCI;
  pipelineCI.pRasterizationState = &rasterizerState;
  pipelineCI.pMultisampleState = &multisampleCI;
  pipelineCI.pDepthStencilState = &dsState;
  pipelineCI.pColorBlendState = &colorBlendStateCI;

  shaderStages = {
    book_util::LoadShader(m_device, "tessVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "tessTCS.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT),
    book_util::LoadShader(m_device, "tessTES.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT),
    book_util::LoadShader(m_device, "tessFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
  };
  viewportStateCI.scissorCount = 1;
  viewportStateCI.pScissors = &scissorBackbuffer;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.pViewports = &viewportBackbuffer;

  pipelineCI.pViewportState = &viewportStateCI;
  pipelineCI.pDynamicState = &pipelineDynamicStateCI;
  pipelineCI.renderPass = GetRenderPass("default");
  pipelineCI.layout = layout;
  pipelineCI.pStages = shaderStages.data();
  pipelineCI.stageCount = uint32_t(shaderStages.size());
  pipelineCI.pTessellationState = &tessStateCI;

  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_tessGroundPipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipelines failed.");

  // ワイヤーフレーム描画用も作成.
  rasterizerState.polygonMode = VK_POLYGON_MODE_LINE;
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_tessGroundWired);
  ThrowIfFailed(result, "vkCreateGraphicsPipelines failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);
}

void TessellateGroundApp::RenderHUD(VkCommandBuffer command)
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  {
    ImGui::Begin("Control");
    auto cameraPos = m_camera.GetPosition();
    ImGui::Text("CameraPos: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);
    ImGui::Checkbox("WireFrame", &m_isWireframe);
    ImGui::End();
  }
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

