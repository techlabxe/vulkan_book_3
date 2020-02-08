#include "TessellateTeapotApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"

#include <array>
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include "stb_image.h"

#include "TeapotPatch.h"


using namespace std;

TessellateTeapotApp::TessellateTeapotApp()
{
  m_camera.SetLookAt(
    glm::vec3(0.0f, 2.0f, 5.0f),
    glm::vec3(0.0f, 0.0f, 0.0f)
  );
  m_tessFactor = 1.0f;
}

void TessellateTeapotApp::Prepare()
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

  PrepareTessTeapot();
}

void TessellateTeapotApp::Cleanup()
{
  DestroyBuffer(m_tessTeapot.resVertexBuffer);
  DestroyBuffer(m_tessTeapot.resIndexBuffer);

  vkDestroyPipeline(m_device, m_tessTeapotPipeline, nullptr);
  for (auto& ubo : m_tessTeapotUniform)
  {
    DestroyBuffer(ubo);
  }

  DestroyImage(m_depthBuffer);
  auto count = uint32_t(m_framebuffers.size());
  DestroyFramebuffers(count, m_framebuffers.data());

  for (auto& c : m_commandBuffers)
  {
    DestroyCommandBuffer(c.commandBuffer);
    DestroyFence(c.fence);
  }
}

bool TessellateTeapotApp::OnMouseButtonDown(int msg)
{
  if (VulkanAppBase::OnMouseButtonDown(msg))
    return true;

  m_camera.OnMouseButtonDown(msg);
  return true;
}

bool TessellateTeapotApp::OnMouseMove(int dx, int dy)
{
  if (VulkanAppBase::OnMouseMove(dx, dy))
    return true;

  m_camera.OnMouseMove(dx, dy);
  return true;
}

bool TessellateTeapotApp::OnMouseButtonUp(int msg)
{
  if (VulkanAppBase::OnMouseButtonUp(msg))
    return true;

  m_camera.OnMouseButtonUp();
  return true;
}

void TessellateTeapotApp::CreateSampleLayouts()
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

void TessellateTeapotApp::Render()
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
      //{ 0.85f, 0.5f, 0.5f, 0.0f}, // for Color
      { 0.25f, 0.25f, 0.25f, 0.0f}, // for Color
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

    TessellationShaderParameters tessParams;
    tessParams.world = glm::mat4(1.0);
    tessParams.view = m_camera.GetViewMatrix();
    tessParams.proj = m_projection;
    tessParams.lightPos = glm::vec4(0.0f);
    tessParams.cameraPos = glm::vec4(m_camera.GetPosition(), 0.0f);
    tessParams.tessOuterLevel = m_tessFactor;
    tessParams.tessInnerLevel = m_tessFactor;
    WriteToHostVisibleMemory(m_tessTeapotUniform[imageIndex].memory, sizeof(tessParams), &tessParams);
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
 
  auto pipelineLayout = GetPipelineLayout("u1");
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessTeapotPipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_dsTeapot[imageIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_tessTeapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdBindVertexBuffers(command, 0, 1, &m_tessTeapot.resVertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_tessTeapot.indexCount, 1, 0, 0, 0);

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



void TessellateTeapotApp::PrepareFramebuffers()
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

bool TessellateTeapotApp::OnSizeChanged(uint32_t width, uint32_t height)
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

void TessellateTeapotApp::PrepareSceneResource()
{
}

void TessellateTeapotApp::PrepareTessTeapot()
{
  auto teapotPoints = TeapotPatch::GetTeapotPatchPoints();
  auto teapotIndices = TeapotPatch::GetTeapotPatchIndices();
  m_tessTeapot = CreateSimpleModel(teapotPoints, teapotIndices);

  auto stride = uint32_t(sizeof(TeapotPatch::ControlPoint));
  VkVertexInputBindingDescription vibDesc{
      0, // binding
      stride,
      VK_VERTEX_INPUT_RATE_VERTEX
  };
  array<VkVertexInputAttributeDescription, 1> inputAttribs{
  {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TeapotPatch::ControlPoint, Position) },
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
  VkPipelineTessellationStateCreateInfo tessStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
    nullptr,
    0, 
    16
  };

  VkPipelineMultisampleStateCreateInfo multisampleCI{
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    nullptr, 0,
    VK_SAMPLE_COUNT_1_BIT,
    VK_FALSE, // sampleShadingEnable
    0.0f, nullptr,
    VK_FALSE, VK_FALSE,
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
  //rasterizerState.polygonMode = VK_POLYGON_MODE_LINE;

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

  VkResult result;

  // メインへの描画用.
  shaderStages = {
    book_util::LoadShader(m_device, "tessTeapotVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "tessTeapotTCS.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT),
    book_util::LoadShader(m_device, "tessTeapotTES.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT),
    book_util::LoadShader(m_device, "tessTeapotFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };
  viewportStateCI.scissorCount = 1;
  viewportStateCI.pScissors = &scissorBackbuffer;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.pViewports = &viewportBackbuffer;

  pipelineCI.pViewportState = &viewportStateCI;
  pipelineCI.pDynamicState = &pipelineDynamicStateCI;
  pipelineCI.renderPass = GetRenderPass("default");
  pipelineCI.layout = GetPipelineLayout("u1");
  pipelineCI.pTessellationState = &tessStateCI;
  pipelineCI.pStages = shaderStages.data();
  pipelineCI.stageCount = uint32_t(shaderStages.size());
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_tessTeapotPipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline failed.");

  auto dsLayout = GetDescriptorSetLayout("u1");
  VkDescriptorSetAllocateInfo dsAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
    m_descriptorPool,
    1, &dsLayout
  };
  auto imageCount = int(m_swapchain->GetImageCount());
  m_dsTeapot.resize(imageCount);
  for (int i = 0; i < imageCount; ++i)
  {
    result = vkAllocateDescriptorSets(m_device, &dsAI, &m_dsTeapot[i]);
    ThrowIfFailed(result, "vkAllocateDescriptorSets failed.");
  }

  m_tessTeapotUniform = CreateUniformBuffers(sizeof(TessellationShaderParameters), imageCount);

  for (int i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo bufferInfo{
      m_tessTeapotUniform[i].buffer,
      0, VK_WHOLE_SIZE
    };
    VkWriteDescriptorSet writeDS = book_util::CreateWriteDescriptorSet(
      m_dsTeapot[i], 0, &bufferInfo
    );
    vkUpdateDescriptorSets(m_device, 1, &writeDS, 0, nullptr);
  }

  book_util::DestroyShaderModules(m_device, shaderStages);
}

void TessellateTeapotApp::RenderHUD(VkCommandBuffer command)
{
  // ImGui
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  ImGui::Begin("Information");
  ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
  //ImGui::Combo("Mode", (int*)&m_mode, "Static\0MultiPass\0SinglePass\0\0");
  ImGui::SliderFloat("TessFactor", &m_tessFactor, 1.0f, 32.0f, "%.1f");
  ImGui::End();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(
    ImGui::GetDrawData(), command
  );
}

