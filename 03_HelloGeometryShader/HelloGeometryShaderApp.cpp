#include "HelloGeometryShaderApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <array>

using namespace std;
using namespace glm;

HelloGeometryShaderApp::HelloGeometryShaderApp()
{
  m_camera.SetLookAt(
    vec3(0.0f, 2.0f, 10.0f),
    vec3(0.0f, 0.0f, 0.0f)
  );
  m_mode = DrawMode_Flat;
}

void HelloGeometryShaderApp::Prepare()
{
  CreateSampleLayouts();

  auto colorFormat = m_swapchain->GetSurfaceFormat().format;
  RegisterRenderPass("default", CreateRenderPass(colorFormat, VK_FORMAT_D32_SFLOAT) );
  
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

  PrepareTeapot();

  CreatePipeline();
}

void HelloGeometryShaderApp::Cleanup()
{
  for (auto& ubo : m_uniformBuffers)
  {
    DestroyBuffer(ubo);
  }
  DestroyBuffer(m_teapot.resVertexBuffer);
  DestroyBuffer(m_teapot.resIndexBuffer);

  for (auto& v : m_descriptorSets)
  {
    DeallocateDescriptorSet(v);
  }
  m_descriptorSets.clear();

  for (auto& v : m_pipelines)
  {
    vkDestroyPipeline(m_device, v.second, nullptr);
  }
  m_pipelines.clear();

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

bool HelloGeometryShaderApp::OnMouseButtonDown(int msg)
{
  if (VulkanAppBase::OnMouseButtonDown(msg))
    return true;

  m_camera.OnMouseButtonDown(msg);
  return true;
}

bool HelloGeometryShaderApp::OnMouseMove(int dx, int dy)
{
  if (VulkanAppBase::OnMouseMove(dx, dy))
    return true;

  m_camera.OnMouseMove(dx, dy);
  return true;
}

bool HelloGeometryShaderApp::OnMouseButtonUp(int msg)
{
  if (VulkanAppBase::OnMouseButtonUp(msg))
    return true;

  m_camera.OnMouseButtonUp();
  return true;
}

void HelloGeometryShaderApp::Render()
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
    // ユニフォームバッファの更新.
    ShaderParameters shaderParams{};
    shaderParams.world = mat4(1.0f);

    shaderParams.view = m_camera.GetViewMatrix();
    auto extent = m_swapchain->GetSurfaceExtent();
    shaderParams.proj = perspectiveRH(
      radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );
    shaderParams.lightDir = vec4(0.0f, 1.0f, 1.0f, 0.0f);

    auto ubo = m_uniformBuffers[imageIndex];
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &shaderParams, sizeof(ShaderParameters));
    vkUnmapMemory(m_device, ubo.memory);
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
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);

  if (m_mode == DrawMode_Flat)
  {
    // フラットシェーディング.
    auto pipeline = m_pipelines[FlatShadePipeine];
    auto layout = GetPipelineLayout("u1");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);
    vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
    vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);
  }

  if (m_mode == DrawMode_NormalVector)
  {
    // 通常の Lambert シェーディングでモデル描画.
    auto pipeline = m_pipelines[SmoothShadePipeline];
    auto layout = GetPipelineLayout("u1");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);
    vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
    vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);

    // 法線描画.
    pipeline = m_pipelines[NormalVectorPipeline];
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);
  }

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



void HelloGeometryShaderApp::PrepareFramebuffers()
{
  auto imageCount = m_swapchain->GetImageCount();
  auto extent = m_swapchain->GetSurfaceExtent();
  auto renderPass = GetRenderPass("default");

  m_framebuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    vector<VkImageView> views;
    views.push_back(m_swapchain->GetImageView(i));
    views.push_back(m_depthBuffer.view);

    m_framebuffers[i] = CreateFramebuffer(
      renderPass,
      extent.width, extent.height,
      uint32_t(views.size()), views.data()
    );
  }
}

bool HelloGeometryShaderApp::OnSizeChanged(uint32_t width, uint32_t height)
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

void HelloGeometryShaderApp::PrepareTeapot()
{
  std::vector<TeapotModel::Vertex> vertices(std::begin(TeapotModel::TeapotVerticesPN), std::end(TeapotModel::TeapotVerticesPN));
  std::vector<uint32_t> indices(std::begin(TeapotModel::TeapotIndices), std::end(TeapotModel::TeapotIndices));
  m_teapot = CreateSimpleModel(vertices, indices);

  auto dsLayout = GetDescriptorSetLayout("u1");

  // ディスクリプタセット.
  auto imageCount = m_swapchain->GetImageCount();
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorSet descriptorSet = AllocateDescriptorSet(dsLayout);
    m_descriptorSets.push_back(descriptorSet);
  }

  // 定数バッファの準備.
  auto bufferSize = uint32_t(sizeof(ShaderParameters));
  m_uniformBuffers = CreateUniformBuffers(bufferSize, imageCount);

  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo bufferInfo{
      m_uniformBuffers[i].buffer,
      0, VK_WHOLE_SIZE
    };

    VkWriteDescriptorSet writeDescSet{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      nullptr,
      m_descriptorSets[i],  // dstSet
      0,
      0, // dstArrayElement
      1, // descriptorCount
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      nullptr,
      &bufferInfo,
      nullptr,
    };
    vkUpdateDescriptorSets(m_device, 1, &writeDescSet, 0, nullptr);
  }
}

void HelloGeometryShaderApp::CreatePipeline()
{
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

  VkResult result;
  auto renderPass = GetRenderPass("default");
  auto layout = GetPipelineLayout("u1");

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


  // パイプライン構築.
  VkGraphicsPipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    nullptr, 0,
    0, nullptr,
    &pipelineVisCI, &inputAssemblyCI,
    nullptr, // Tessellation
    &viewportCI, // ViewportState
    &rasterizerState,
    &multisampleCI,
    &dsState,
    &colorBlendStateCI,
    &pipelineDynamicStateCI, // DynamicState
    layout,
    renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };


  {
    // フラットシェーディング用パイプラインの構築.
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages
    {
      book_util::LoadShader(m_device, "flatVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
      book_util::LoadShader(m_device, "flatGS.spv", VK_SHADER_STAGE_GEOMETRY_BIT),
      book_util::LoadShader(m_device, "flatFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
    };
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.stageCount = uint32_t(shaderStages.size());

    VkPipeline pipeline;
    result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
    ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

    book_util::DestroyShaderModules(m_device, shaderStages);
    m_pipelines[FlatShadePipeine] = pipeline;
  }

  {
    // 法線描画用パイプラインの構築.
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages
    {
      book_util::LoadShader(m_device, "drawNormalVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
      book_util::LoadShader(m_device, "drawNormalGS.spv", VK_SHADER_STAGE_GEOMETRY_BIT),
      book_util::LoadShader(m_device, "drawNormalFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
    };
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.stageCount = uint32_t(shaderStages.size());

    VkPipeline pipeline;
    result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
    ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

    book_util::DestroyShaderModules(m_device, shaderStages);
    m_pipelines[NormalVectorPipeline] = pipeline;
  }
  {
    // 法線描画時のモデル本体描画パイプラインの構築.
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages
    {
      book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
      book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
    };
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.stageCount = uint32_t(shaderStages.size());

    VkPipeline pipeline;
    result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
    ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

    book_util::DestroyShaderModules(m_device, shaderStages);
    m_pipelines[SmoothShadePipeline] = pipeline;
  }

}

void HelloGeometryShaderApp::RenderHUD(VkCommandBuffer command)
{
  // ImGui
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  ImGui::Begin("Information");
  ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
  ImGui::Combo("Mode", (int*)&m_mode, "Flat\0NormalVector\0\0");
  ImGui::End();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(
    ImGui::GetDrawData(), command
  );
}

void HelloGeometryShaderApp::CreateSampleLayouts()
{
  // ディスクリプタセットレイアウトの準備.
  std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings;
  VkDescriptorSetLayoutCreateInfo dsLayoutCI{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
  };
  VkResult result;
  VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;

  // 0: uniformBuffer
  dsLayoutBindings = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, },
  };
  dsLayoutCI.pBindings = dsLayoutBindings.data();
  dsLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
  
  result = vkCreateDescriptorSetLayout(m_device, &dsLayoutCI, nullptr, &dsLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed (u1).");
  RegisterLayout("u1", dsLayout); dsLayout = VK_NULL_HANDLE;


  // パイプラインレイアウトの準備.
  VkPipelineLayoutCreateInfo layoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0,
  };
  VkPipelineLayout layout;
  dsLayout = GetDescriptorSetLayout("u1");
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &dsLayout;
  result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed(u1).");
  RegisterLayout("u1", layout); layout = VK_NULL_HANDLE;

}
