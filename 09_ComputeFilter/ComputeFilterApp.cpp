#include "ComputeFilterApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"


#include <array>

#include <glm/gtc/matrix_transform.hpp>

#include "stb_image.h"

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

using namespace std;

ComputeFilterApp::ComputeFilterApp()
{
  m_selectedFilter = 0;
}

void ComputeFilterApp::Prepare()
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

  PrepareComputeResource();
  CreatePrimitiveResource();
}

void ComputeFilterApp::CreateSampleLayouts()
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

  dsLayoutBindings = {
    { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, },
    { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, },
  };
  dsLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
  dsLayoutCI.pBindings = dsLayoutBindings.data();
  result = vkCreateDescriptorSetLayout(m_device, &dsLayoutCI, nullptr, &dsLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
  RegisterLayout("compute_filter", dsLayout);


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

  dsLayout = GetDescriptorSetLayout("compute_filter");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("compute_filter", layout);
}

void ComputeFilterApp::Cleanup()
{
  DestroyBuffer(m_quad.resVertexBuffer);
  DestroyBuffer(m_quad.resIndexBuffer);

  DestroyBuffer(m_quad2.resVertexBuffer);
  DestroyBuffer(m_quad2.resIndexBuffer);

  DestroyImage(m_sourceBuffer);
  DestroyImage(m_destBuffer);

  vkDestroySampler(m_device, m_texSampler, nullptr);

  vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &m_dsWriteToTexture);
  for (auto ds : m_dsDrawTextures)
  {
    vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(ds.size()), ds.data());
  }
  for (auto& ubo : m_shaderUniforms)
  {
    DestroyBuffer(ubo);
  }

  vkDestroyPipeline(m_device, m_pipeline, nullptr);
  vkDestroyPipeline(m_device, m_compSepiaPipeline, nullptr);
  vkDestroyPipeline(m_device, m_compSobelPipeline, nullptr);

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

void ComputeFilterApp::Render()
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
    m_projection = glm::ortho(-640.0f, 640.0f, -360.0f, 360.0f, -100.0f, 100.0f);
  }

  {
    ShaderParameters shaderParams{};
    auto extent = m_swapchain->GetSurfaceExtent();
    shaderParams.proj = m_projection;

    void* p;
    vkMapMemory(m_device, m_shaderUniforms[imageIndex].memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &shaderParams, sizeof(shaderParams));
    vkUnmapMemory(m_device, m_shaderUniforms[imageIndex].memory);
  }

  auto fence = m_commandBuffers[imageIndex].fence;
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

  auto command = m_commandBuffers[imageIndex].commandBuffer;

  vkBeginCommandBuffer(command, &commandBI);

  // グラフィックスもサポートするキューでは、レンダーパスの外でコンピュートシェーダーは実行する必要がある.
  auto pipelineLayout = GetPipelineLayout("compute_filter");
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &m_dsWriteToTexture, 0, nullptr);
  if (m_selectedFilter == 0)
  {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_compSepiaPipeline);
  }
  if (m_selectedFilter == 1)
  {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_compSobelPipeline);
  }
  int groupX = 1280 / 16 + 1;
  int groupY = 720 / 16 + 1;
  vkCmdDispatch(command, groupX, groupY, 1);

  // 書き込んだ内容をテクスチャとして参照するためにレイアウト変更.
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &CreateImageMemoryBarrier(m_destBuffer.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &CreateImageMemoryBarrier(m_sourceBuffer.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0},
    extent
  };
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);

  VkDeviceSize offsets[1] = { 0 };
  pipelineLayout = GetPipelineLayout("u1t1");
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_dsDrawTextures[0][imageIndex], 0, nullptr);
  vkCmdBindVertexBuffers(command, 0, 1, &m_quad.resVertexBuffer.buffer, offsets);
  vkCmdBindIndexBuffer(command, m_quad.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(command, m_quad.indexCount, 1, 0, 0, 0);

  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_dsDrawTextures[1][imageIndex], 0, nullptr);
  vkCmdBindVertexBuffers(command, 0, 1, &m_quad2.resVertexBuffer.buffer, offsets);
  vkCmdBindIndexBuffer(command, m_quad2.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(command, m_quad2.indexCount, 1, 0, 0, 0);

  RenderHUD(command);

  vkCmdEndRenderPass(command);

  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &CreateImageMemoryBarrier(m_sourceBuffer.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL));
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &CreateImageMemoryBarrier(m_destBuffer.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL));

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

void ComputeFilterApp::PrepareFramebuffers()
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

bool ComputeFilterApp::OnSizeChanged(uint32_t width, uint32_t height)
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
void ComputeFilterApp::CreatePrimitiveResource()
{
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
    nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
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

  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0},
    extent
  };
  VkPipelineViewportStateCreateInfo viewportCI{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &viewport,
    1, &scissor,
  };

  // シェーダーのロード.
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages
  {
    book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };

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
  auto pipelineLayout = GetPipelineLayout("u1t1");

  VkResult result;
  // パイプライン構築.
  VkGraphicsPipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    nullptr, 0,
    uint32_t(shaderStages.size()), shaderStages.data(),
    &pipelineVisCI, &inputAssemblyCI,
    nullptr, // Tessellation
    &viewportCI, // ViewportState
    &rasterizerState,
    &multisampleCI,
    &dsState,
    &colorBlendStateCI,
    &pipelineDynamicStateCI, // DynamicState
    pipelineLayout,
    GetRenderPass("default"),
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);

  // 描画用のパイプラインで使用するディスクリプタセットの準備.
  int imageCount = m_swapchain->GetImageCount();
  auto dsLayout = GetDescriptorSetLayout("u1t1");
  VkDescriptorSetAllocateInfo dsAI = {
  VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
  nullptr, m_descriptorPool,
  1, &dsLayout
  };

  VkDescriptorImageInfo textureImage[] = {
    // 変換元テクスチャ.
    { m_texSampler, m_sourceBuffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, },
    // 変換先テクスチャ.
    { m_texSampler, m_destBuffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, },
  };

  for (int type = 0; type < 2; ++type)
  {
    m_dsDrawTextures[type].resize(imageCount);


    for (int i = 0; i < imageCount; ++i)
    {
      auto& descriptorSet = m_dsDrawTextures[type][i];

      result = vkAllocateDescriptorSets(m_device, &dsAI, &descriptorSet);
      ThrowIfFailed(result, "vkAllocateDescriptorSets failed.");

      VkDescriptorBufferInfo ubo = { m_shaderUniforms[i].buffer, 0, VK_WHOLE_SIZE };
      VkDescriptorImageInfo tex = textureImage[type];

      std::vector<VkWriteDescriptorSet> writeDS = {
        book_util::CreateWriteDescriptorSet(descriptorSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &ubo),
        book_util::CreateWriteDescriptorSet(descriptorSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &tex),
      };
      vkUpdateDescriptorSets(m_device, uint32_t(writeDS.size()), writeDS.data(), 0, nullptr);
    }
  }

}

void ComputeFilterApp::PrepareSceneResource()
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

  m_sourceBuffer = Load2DTextureFromFile("image.png", VK_IMAGE_LAYOUT_GENERAL);
}

ComputeFilterApp::ImageObject ComputeFilterApp::Load2DTextureFromFile(const char* fileName, VkImageLayout layout)
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
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
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
  imb.newLayout = layout;
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

void ComputeFilterApp::PrepareComputeResource()
{
  using namespace glm;
  float offset = 10.0f;
  std::vector<Vertex> vertices;
  
  vertices = {
    { vec3(-480.0f - offset, -135.0f, 0.0f), vec2(0.0f, 1.0f),},
    { vec3(   0.0f - offset, -135.0f, 0.0f), vec2(1.0f, 1.0f),},
    { vec3(-480.0f - offset,  135.0f, 0.0f), vec2(0.0f, 0.0f),},
    { vec3(   0.0f - offset,  135.0f, 0.0f), vec2(1.0f, 0.0f),},
  };
  std::vector<uint32_t> indices = {
    0, 1, 2, 3
  };
  m_quad = CreateSimpleModel(vertices, indices);
  
  vertices = {
    { vec3(+480.0f + offset, -135.0f, 0.0f), vec2(1.0f, 1.0f),},
    { vec3(   0.0f + offset, -135.0f, 0.0f), vec2(0.0f, 1.0f),},
    { vec3(+480.0f + offset,  135.0f, 0.0f), vec2(1.0f, 0.0f),},
    { vec3(   0.0f + offset,  135.0f, 0.0f), vec2(0.0f, 0.0f),},
  };
  m_quad2 = CreateSimpleModel(vertices, indices);

  int width = 1280, height = 720;
  {
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkImageCreateInfo imageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr,
      0,
      VK_IMAGE_TYPE_2D,
      VK_FORMAT_R8G8B8A8_UNORM, { uint32_t(width), uint32_t(height), 1u },
      1, 1,
      VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0, nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED
    };
    VkImage image;
    vkCreateImage(m_device, &imageCI, nullptr, &image);
    VkDeviceMemory memory;
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(m_device, image, &reqs);
    VkMemoryAllocateInfo info{
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
      reqs.size,
      GetMemoryTypeIndex(reqs.memoryTypeBits, memProps)
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
    vkCreateImageView(m_device, &viewCI, nullptr, &view);

    m_destBuffer.image = image;
    m_destBuffer.view = view;
    m_destBuffer.memory = memory;
  }

  {
    auto command = CreateCommandBuffer();

    VkImageMemoryBarrier imageLayoutSrc{
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
    };
    imageLayoutSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageLayoutSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageLayoutSrc.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageLayoutSrc.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageLayoutSrc.image = m_sourceBuffer.image;
    imageLayoutSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier imageLayoutDst{
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
    };
    imageLayoutDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageLayoutDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageLayoutDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageLayoutDst.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageLayoutDst.image = m_destBuffer.image;
    imageLayoutDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    imageLayoutSrc.srcAccessMask = 0;
    imageLayoutSrc.dstAccessMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    imageLayoutDst.srcAccessMask = 0;
    imageLayoutDst.dstAccessMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    std::vector<VkImageMemoryBarrier> barriers = {
      imageLayoutSrc, imageLayoutDst,
    };

    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    vkCmdPipelineBarrier(command, 
      srcStageMask,
      dstStageMask, 0,
      0, nullptr, // memoryBarriers,
      0, nullptr, // BufferBarriers,
      uint32_t(barriers.size()), barriers.data() // imageMemoryBarriers
    );
    FinishCommandBuffer(command);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
  }
  auto imageCount = int(m_swapchain->GetImageCount());
  m_shaderUniforms = CreateUniformBuffers(sizeof(ShaderParameters), imageCount);

  VkResult result;
  VkDescriptorSetLayout dsLayout = GetDescriptorSetLayout("compute_filter");
  VkDescriptorSetAllocateInfo dsAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &dsLayout
  };
  result = vkAllocateDescriptorSets(m_device, &dsAI, &m_dsWriteToTexture);
  ThrowIfFailed(result, "vkAllocateDescriptorSets failed.");

  VkDescriptorImageInfo sourceImage = {
    m_texSampler, m_sourceBuffer.view, VK_IMAGE_LAYOUT_GENERAL
  };
  VkDescriptorImageInfo destImage = {
    m_texSampler, m_destBuffer.view, VK_IMAGE_LAYOUT_GENERAL,
  };
  std::vector<VkWriteDescriptorSet> writeDS = {
      book_util::CreateWriteDescriptorSet(m_dsWriteToTexture, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &sourceImage),
      book_util::CreateWriteDescriptorSet(m_dsWriteToTexture, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destImage),
  };
  vkUpdateDescriptorSets(m_device, uint32_t(writeDS.size()), writeDS.data(), 0, nullptr);

  // パイプラインレイアウトの準備
  VkPipelineLayout layout = GetPipelineLayout("compute_filter");

  // パイプライン構築.
  auto computeStage = book_util::LoadShader(m_device, "sepiaCS.spv", VK_SHADER_STAGE_COMPUTE_BIT);

  VkComputePipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
    computeStage,
    layout,
    VK_NULL_HANDLE,
    0,
  };
  result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_compSepiaPipeline);
  ThrowIfFailed(result, "vkCreateComputePipelines failed.");
  vkDestroyShaderModule(m_device, computeStage.module, nullptr);
  
  computeStage = book_util::LoadShader(m_device, "sobelCS.spv", VK_SHADER_STAGE_COMPUTE_BIT);
  pipelineCI.stage = computeStage;
  result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_compSobelPipeline);
  ThrowIfFailed(result, "vkCreateComputePipelines failed.");
  vkDestroyShaderModule(m_device, computeStage.module, nullptr);
  
}

void ComputeFilterApp::RenderHUD(VkCommandBuffer command)
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  auto framerate = ImGui::GetIO().Framerate;
  ImGui::Begin("Control");
  ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);

  ImGui::Combo("Filter", &m_selectedFilter, "Sepia Filter\0Sobel Filter\0\0");
  ImGui::End();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

ComputeFilterApp::BufferObject ComputeFilterApp::CreateStorageBuffer(size_t bufferSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags props)
{
  usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
  BufferObject obj;
  VkBufferCreateInfo bufferCI{
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    nullptr, 0,
    bufferSize, usage,
    VK_SHARING_MODE_EXCLUSIVE,
    0, nullptr
  };
  auto result = vkCreateBuffer(m_device, &bufferCI, nullptr, &obj.buffer);
  ThrowIfFailed(result, "vkCreateBuffer Failed.");

  // メモリ量の算出.
  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(m_device, obj.buffer, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, props)
  };
  vkAllocateMemory(m_device, &info, nullptr, &obj.memory);
  vkBindBufferMemory(m_device, obj.buffer, obj.memory, 0);
  return obj;
}

VkImageMemoryBarrier ComputeFilterApp::CreateImageMemoryBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
  VkImageMemoryBarrier imageLayoutDst{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
  };
  imageLayoutDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageLayoutDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageLayoutDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

  imageLayoutDst.oldLayout = oldLayout;
  imageLayoutDst.newLayout = newLayout;
  imageLayoutDst.image = image;

  switch (oldLayout)
  {
  case VK_IMAGE_LAYOUT_UNDEFINED:
    imageLayoutDst.srcAccessMask = 0;
    break;
  case VK_IMAGE_LAYOUT_GENERAL:
    imageLayoutDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    break;
  }
  switch (newLayout)
  {
  case VK_IMAGE_LAYOUT_GENERAL:
    imageLayoutDst.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    imageLayoutDst.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;
  }
  return imageLayoutDst;
}





