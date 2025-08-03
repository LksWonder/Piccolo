#include "runtime/function/render/passes/scan_pass.h"

#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#include "runtime/function/render/interface/vulkan/vulkan_util.h"

#include <post_process_vert.h>
#include <scan_frag.h>

#include <stdexcept>

namespace Piccolo
{
    void ScanPass::initialize(const RenderPassInitInfo* init_info)
    {
        RenderPass::initialize(nullptr);

        const ScanPassInitInfo* _init_info = static_cast<const ScanPassInitInfo*>(init_info);
        m_framebuffer.render_pass                 = _init_info->render_pass;

        createCommandBuffer();
        prepareUniformBuffer();
        recreateImageView();
        setupDescriptorSetLayout();
        setupPipelines();
        setupDescriptorSet();
        updateAfterFramebufferRecreate(_init_info->input_attachment);
    }

    void ScanPass::prepareUniformBuffer()
    {
        RHIDeviceMemory* d_mem;
        m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         m_uniform_buffer,
                                         d_mem,
                                         sizeof(m_scan_resource_data));

        if (RHI_SUCCESS != m_rhi->mapMemory(d_mem, 0, RHI_WHOLE_SIZE, 0, &m_uniform_buffer_mapped))
        {
            throw std::runtime_error("map billboard uniform buffer");
        }

        m_scan_resource_data.scan_distance = 1;
        memcpy(m_uniform_buffer_mapped, &m_scan_resource_data, sizeof(m_scan_resource_data));
    }

    void ScanPass::setupDescriptorSetLayout()
    {
        m_descriptor_infos.resize(1);

        RHIDescriptorSetLayoutBinding post_process_global_layout_bindings[4] = {};

        RHIDescriptorSetLayoutBinding& post_process_global_layout_input_attachment_binding = post_process_global_layout_bindings[0];
        post_process_global_layout_input_attachment_binding.binding         = 0;
        post_process_global_layout_input_attachment_binding.descriptorType  = RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        post_process_global_layout_input_attachment_binding.descriptorCount = 1;
        post_process_global_layout_input_attachment_binding.stageFlags      = RHI_SHADER_STAGE_FRAGMENT_BIT;

        RHIDescriptorSetLayoutBinding& depth_texture_binding          = post_process_global_layout_bindings[1];
        depth_texture_binding.binding                                 = 1;
        depth_texture_binding.descriptorType                          = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        depth_texture_binding.descriptorCount                         = 1;
        depth_texture_binding.stageFlags                              = RHI_SHADER_STAGE_FRAGMENT_BIT;
        depth_texture_binding.pImmutableSamplers                      = NULL;

        RHIDescriptorSetLayoutCreateInfo post_process_global_layout_create_info;
        post_process_global_layout_create_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        post_process_global_layout_create_info.pNext = NULL;
        post_process_global_layout_create_info.flags = 0;
        post_process_global_layout_create_info.bindingCount = sizeof(post_process_global_layout_bindings) / sizeof(post_process_global_layout_bindings[0]);
        post_process_global_layout_create_info.pBindings = post_process_global_layout_bindings;

        // scene depth and normal binding
        {
            RHIDescriptorSetLayoutBinding& gbuffer_normal_global_layout_input_attachment_binding =
                post_process_global_layout_bindings[2];
            gbuffer_normal_global_layout_input_attachment_binding.binding         = 2;
            gbuffer_normal_global_layout_input_attachment_binding.descriptorType =
                RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_normal_global_layout_input_attachment_binding.descriptorCount = 1;
            gbuffer_normal_global_layout_input_attachment_binding.stageFlags      = RHI_SHADER_STAGE_FRAGMENT_BIT;

            RHIDescriptorSetLayoutBinding& gbuffer_depth_global_layout_input_attachment_binding =
                post_process_global_layout_bindings[3];
            gbuffer_depth_global_layout_input_attachment_binding.binding = 3;
            gbuffer_depth_global_layout_input_attachment_binding.descriptorType =
                RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_depth_global_layout_input_attachment_binding.descriptorCount = 1;
            gbuffer_depth_global_layout_input_attachment_binding.stageFlags      = RHI_SHADER_STAGE_FRAGMENT_BIT;
        }

        if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&post_process_global_layout_create_info, m_descriptor_infos[0].layout))
        {
            throw std::runtime_error("create post process global layout");
        }
    }
    void ScanPass::setupPipelines()
    {
        m_render_pipelines.resize(1);

        RHIDescriptorSetLayout*      descriptorset_layouts[1] = {m_descriptor_infos[0].layout};
        RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
        pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

        if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[0].layout) != RHI_SUCCESS)
        {
            throw std::runtime_error("create post process pipeline layout");
        }

        RHIShader* vert_shader_module = m_rhi->createShaderModule(POST_PROCESS_VERT);
        RHIShader* frag_shader_module = m_rhi->createShaderModule(SCAN_FRAG);

        RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
        vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
        vert_pipeline_shader_stage_create_info.module = vert_shader_module;
        vert_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
        frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
        frag_pipeline_shader_stage_create_info.module = frag_shader_module;
        frag_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                           frag_pipeline_shader_stage_create_info};

        RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
        vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_state_create_info.vertexBindingDescriptionCount   = 0;
        vertex_input_state_create_info.pVertexBindingDescriptions      = NULL;
        vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
        vertex_input_state_create_info.pVertexAttributeDescriptions    = NULL;

        RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
        input_assembly_create_info.sType                  = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly_create_info.topology               = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

        RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
        viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state_create_info.viewportCount = 1;
        viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
        viewport_state_create_info.scissorCount  = 1;
        viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

        RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
        rasterization_state_create_info.sType            = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization_state_create_info.depthClampEnable = RHI_FALSE;
        rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
        rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
        rasterization_state_create_info.lineWidth               = 1.0f;
        rasterization_state_create_info.cullMode                = RHI_CULL_MODE_BACK_BIT;
        rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_CLOCKWISE;
        rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
        rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
        rasterization_state_create_info.depthBiasClamp          = 0.0f;
        rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

        RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
        multisample_state_create_info.sType                = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
        multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

        RHIPipelineColorBlendAttachmentState color_blend_attachment_state {};
        color_blend_attachment_state.colorWriteMask      = RHI_COLOR_COMPONENT_R_BIT |
                                                           RHI_COLOR_COMPONENT_G_BIT |
                                                           RHI_COLOR_COMPONENT_B_BIT |
                                                           RHI_COLOR_COMPONENT_A_BIT;
        color_blend_attachment_state.blendEnable         = RHI_FALSE;
        color_blend_attachment_state.srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstColorBlendFactor = RHI_BLEND_FACTOR_ZERO;
        color_blend_attachment_state.colorBlendOp        = RHI_BLEND_OP_ADD;
        color_blend_attachment_state.srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstAlphaBlendFactor = RHI_BLEND_FACTOR_ZERO;
        color_blend_attachment_state.alphaBlendOp        = RHI_BLEND_OP_ADD;

        RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info {};
        color_blend_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend_state_create_info.logicOpEnable     = RHI_FALSE;
        color_blend_state_create_info.logicOp           = RHI_LOGIC_OP_COPY;
        color_blend_state_create_info.attachmentCount   = 1;
        color_blend_state_create_info.pAttachments      = &color_blend_attachment_state;
        color_blend_state_create_info.blendConstants[0] = 0.0f;
        color_blend_state_create_info.blendConstants[1] = 0.0f;
        color_blend_state_create_info.blendConstants[2] = 0.0f;
        color_blend_state_create_info.blendConstants[3] = 0.0f;

        RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
        depth_stencil_create_info.sType                 = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil_create_info.depthTestEnable       = RHI_TRUE;
        depth_stencil_create_info.depthWriteEnable      = RHI_TRUE;
        depth_stencil_create_info.depthCompareOp        = RHI_COMPARE_OP_LESS;
        depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
        depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

        RHIDynamicState dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};

        RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
        dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state_create_info.dynamicStateCount = 2;
        dynamic_state_create_info.pDynamicStates    = dynamic_states;

        RHIGraphicsPipelineCreateInfo pipelineInfo {};
        pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = shader_stages;
        pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
        pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
        pipelineInfo.pViewportState      = &viewport_state_create_info;
        pipelineInfo.pRasterizationState = &rasterization_state_create_info;
        pipelineInfo.pMultisampleState   = &multisample_state_create_info;
        pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
        pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
        pipelineInfo.layout              = m_render_pipelines[0].layout;
        pipelineInfo.renderPass          = m_framebuffer.render_pass;
        pipelineInfo.subpass             = _main_camera_subpass_scan;
        pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
        pipelineInfo.pDynamicState       = &dynamic_state_create_info;

        if (RHI_SUCCESS != m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE, 1, &pipelineInfo, m_render_pipelines[0].pipeline))
        {
            throw std::runtime_error("create post process graphics pipeline");
        }

        m_rhi->destroyShaderModule(vert_shader_module);
        m_rhi->destroyShaderModule(frag_shader_module);
    }
    void ScanPass::setupDescriptorSet()
    {
        RHIDescriptorSetAllocateInfo post_process_global_descriptor_set_alloc_info;
        post_process_global_descriptor_set_alloc_info.sType          = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        post_process_global_descriptor_set_alloc_info.pNext          = NULL;
        post_process_global_descriptor_set_alloc_info.descriptorPool = m_rhi->getDescriptorPoor();
        post_process_global_descriptor_set_alloc_info.descriptorSetCount = 1;
        post_process_global_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[0].layout;

        if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&post_process_global_descriptor_set_alloc_info, 
                                                         m_descriptor_infos[0].descriptor_set))
        {
            throw std::runtime_error("allocate post process global descriptor set");
        }
    }

    void ScanPass::updateAfterFramebufferRecreate(RHIImageView* input_attachment)
    {
        recreateImageView();

        RHIDescriptorImageInfo post_process_per_frame_input_attachment_info = {};
        post_process_per_frame_input_attachment_info.sampler     = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Nearest);
        post_process_per_frame_input_attachment_info.imageView   = input_attachment;
        post_process_per_frame_input_attachment_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        RHIWriteDescriptorSet post_process_descriptor_writes_info[4];

        RHIWriteDescriptorSet& post_process_descriptor_input_attachment_write_info = post_process_descriptor_writes_info[0];
        post_process_descriptor_input_attachment_write_info.sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        post_process_descriptor_input_attachment_write_info.pNext           = NULL;
        post_process_descriptor_input_attachment_write_info.dstSet          = m_descriptor_infos[0].descriptor_set;
        post_process_descriptor_input_attachment_write_info.dstBinding      = 0;
        post_process_descriptor_input_attachment_write_info.dstArrayElement = 0;
        post_process_descriptor_input_attachment_write_info.descriptorType  = RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        post_process_descriptor_input_attachment_write_info.descriptorCount = 1;
        post_process_descriptor_input_attachment_write_info.pImageInfo      = &post_process_per_frame_input_attachment_info;

        RHIDescriptorBufferInfo uniformBufferDescriptor = {m_uniform_buffer, 0, RHI_WHOLE_SIZE};
        {
            RHIWriteDescriptorSet& descriptorset = post_process_descriptor_writes_info[1];
            descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorset.pNext                  = NULL;
            descriptorset.dstSet                 = m_descriptor_infos[0].descriptor_set;
            descriptorset.dstArrayElement        = 0;  // 从别处抄了一个，发现没有这个，没有初始化的话，这个值是随机值。
            descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorset.dstBinding             = 1;
            descriptorset.pBufferInfo            = &uniformBufferDescriptor;
            descriptorset.descriptorCount        = 1;
        }

        {
            RHISampler*          sampler0;
            RHISamplerCreateInfo samplerCreateInfo {};
            samplerCreateInfo.sType            = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.maxAnisotropy    = 1.0f;
            samplerCreateInfo.anisotropyEnable = true;
            samplerCreateInfo.magFilter        = RHI_FILTER_NEAREST;
            samplerCreateInfo.minFilter        = RHI_FILTER_NEAREST;
            samplerCreateInfo.mipmapMode       = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.addressModeU     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeV     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeW     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.mipLodBias       = 0.0f;
            samplerCreateInfo.compareOp        = RHI_COMPARE_OP_NEVER;
            samplerCreateInfo.minLod           = 0.0f;
            samplerCreateInfo.maxLod           = 0.0f;
            samplerCreateInfo.borderColor      = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            if (RHI_SUCCESS != m_rhi->createSampler(&samplerCreateInfo, sampler0))
            {
                throw std::runtime_error("create sampler error");
            }

            RHIDescriptorImageInfo gbuffer_normal_descriptor_image_info = {};
            gbuffer_normal_descriptor_image_info.sampler                = sampler0;
            gbuffer_normal_descriptor_image_info.imageView              = m_src_normal_image_view;
            gbuffer_normal_descriptor_image_info.imageLayout            = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            {

                RHIWriteDescriptorSet& gbuffer_normal_descriptor_input_attachment_write_info =
                    post_process_descriptor_writes_info[2];
                gbuffer_normal_descriptor_input_attachment_write_info.sType  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                gbuffer_normal_descriptor_input_attachment_write_info.pNext  = NULL;
                gbuffer_normal_descriptor_input_attachment_write_info.dstSet = m_descriptor_infos[0].descriptor_set;
                gbuffer_normal_descriptor_input_attachment_write_info.dstBinding      = 2;
                gbuffer_normal_descriptor_input_attachment_write_info.dstArrayElement = 0;
                gbuffer_normal_descriptor_input_attachment_write_info.descriptorType =
                    RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                gbuffer_normal_descriptor_input_attachment_write_info.descriptorCount = 1;
                gbuffer_normal_descriptor_input_attachment_write_info.pImageInfo =
                    &gbuffer_normal_descriptor_image_info;
            }

            RHISampler*          sampler;
            //RHISamplerCreateInfo samplerCreateInfo {};
            samplerCreateInfo.sType            = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.maxAnisotropy    = 1.0f;
            samplerCreateInfo.anisotropyEnable = true;
            samplerCreateInfo.magFilter        = RHI_FILTER_NEAREST;
            samplerCreateInfo.minFilter        = RHI_FILTER_NEAREST;
            samplerCreateInfo.mipmapMode       = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.addressModeU     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeV     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeW     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.mipLodBias       = 0.0f;
            samplerCreateInfo.compareOp        = RHI_COMPARE_OP_NEVER;
            samplerCreateInfo.minLod           = 0.0f;
            samplerCreateInfo.maxLod           = 0.0f;
            samplerCreateInfo.borderColor      = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            if (RHI_SUCCESS != m_rhi->createSampler(&samplerCreateInfo, sampler))
            {
                throw std::runtime_error("create sampler error");
            }

            RHIDescriptorImageInfo depth_descriptor_image_info = {};
            depth_descriptor_image_info.sampler                = sampler;
            depth_descriptor_image_info.imageView              = m_src_depth_image_view;
            depth_descriptor_image_info.imageLayout            = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            {
                RHIWriteDescriptorSet& depth_descriptor_input_attachment_write_info =
                    post_process_descriptor_writes_info[3];
                depth_descriptor_input_attachment_write_info.sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                depth_descriptor_input_attachment_write_info.pNext           = NULL;
                depth_descriptor_input_attachment_write_info.dstSet          = m_descriptor_infos[0].descriptor_set;
                depth_descriptor_input_attachment_write_info.dstBinding      = 3;
                depth_descriptor_input_attachment_write_info.dstArrayElement = 0;
                depth_descriptor_input_attachment_write_info.descriptorType =
                    RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                depth_descriptor_input_attachment_write_info.descriptorCount = 1;
                depth_descriptor_input_attachment_write_info.pImageInfo      = &depth_descriptor_image_info;
            }
        }
        m_rhi->updateDescriptorSets(sizeof(post_process_descriptor_writes_info) /
                                    sizeof(post_process_descriptor_writes_info[0]),
                                    post_process_descriptor_writes_info,
                                    0,
                                    NULL);
    }

    void ScanPass::recreateImageView() 
    {
        if (m_dst_depth_image)
        {
            m_rhi->destroyImage(m_dst_depth_image);
            m_rhi->freeMemory(m_dst_depth_image_memory);
        }

        m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                           m_rhi->getSwapchainInfo().extent.height,
                           m_rhi->getDepthImageInfo().depth_image_format,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_SAMPLED_BIT | RHI_IMAGE_USAGE_TRANSFER_DST_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_dst_depth_image,
                           m_dst_depth_image_memory,
                           0,
                           1,
                           1);

        if (m_dst_normal_image)
        {
            m_rhi->destroyImage(m_dst_normal_image);
            m_rhi->freeMemory(m_dst_normal_image_memory);
        }

        m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                           m_rhi->getSwapchainInfo().extent.height,
                           RHI_FORMAT_R8G8B8A8_UNORM,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_SAMPLED_BIT | RHI_IMAGE_USAGE_TRANSFER_DST_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_dst_normal_image,
                           m_dst_normal_image_memory,
                           0,
                           1,
                           1);

        m_rhi->createImageView(m_dst_depth_image,
                               m_rhi->getDepthImageInfo().depth_image_format,
                               RHI_IMAGE_ASPECT_DEPTH_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_src_depth_image_view);

        m_rhi->createImageView(m_dst_normal_image,
                               RHI_FORMAT_R8G8B8A8_UNORM,
                               RHI_IMAGE_ASPECT_COLOR_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_src_normal_image_view);
    }

    void ScanPass::preparePassData(std::shared_ptr<RenderResourceBase> render_resource)
    {
        const RenderResource* vulkan_resource = static_cast<const RenderResource*>(render_resource.get());
        if (vulkan_resource)
        {
            m_scan_resource_data = vulkan_resource->m_scan_resource_data;
            m_scan_resource_data.scan_distance = 0;
            memcpy(m_uniform_buffer_mapped, &m_scan_resource_data, sizeof(ScanResourceData));
        }
    }

    void ScanPass::createCommandBuffer()
    {
        RHICommandBufferAllocateInfo cmdBufAllocateInfo {};
        cmdBufAllocateInfo.sType              = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool        = m_rhi->getCommandPoor();
        cmdBufAllocateInfo.level              = RHI_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = 1;
        if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&cmdBufAllocateInfo, m_copy_command_buffer))
            throw std::runtime_error("alloc copy command buffer");

        RHIFenceCreateInfo fenceCreateInfo {};
        fenceCreateInfo.sType = RHI_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        if (RHI_SUCCESS != m_rhi->createFence(&fenceCreateInfo, m_fence))
            throw std::runtime_error("create fence");
    }

    void ScanPass::copyNormalAndDepthImage() 
    {
        uint8_t index =
            (m_rhi->getCurrentFrameIndex() + m_rhi->getMaxFramesInFlight() - 1) % m_rhi->getMaxFramesInFlight();

        // fence作用：? 这里用了跟particle一样的做法，但是好像有点问题？最后采用新增fence。
        // m_rhi->waitForFencesPFN(1, &(m_rhi->getFenceList()[index]), VK_TRUE, UINT64_MAX);
       // m_rhi->waitForFencesPFN(1, &m_fence, VK_TRUE, UINT64_MAX);

        RHICommandBufferBeginInfo command_buffer_begin_info {};
        command_buffer_begin_info.sType            = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags            = 0;
        command_buffer_begin_info.pInheritanceInfo = nullptr;

        bool res_begin_command_buffer = m_rhi->beginCommandBufferPFN(m_copy_command_buffer, &command_buffer_begin_info);
        assert(RHI_SUCCESS == res_begin_command_buffer);

        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        m_rhi->pushEvent(m_copy_command_buffer, "Copy Depth Image for Scan", color);

        // depth image
        RHIImageSubresourceRange subresourceRange = {RHI_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        RHIImageMemoryBarrier    imagememorybarrier {};
        imagememorybarrier.sType               = RHI_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imagememorybarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        imagememorybarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        imagememorybarrier.subresourceRange    = subresourceRange;
        {
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_UNDEFINED;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.srcAccessMask = 0;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.image         = m_dst_depth_image;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_UNDEFINED;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_READ_BIT;
            imagememorybarrier.image         = m_src_depth_image;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            m_rhi->cmdCopyImageToImage(m_copy_command_buffer,
                                       m_src_depth_image,
                                       RHI_IMAGE_ASPECT_DEPTH_BIT,
                                       m_dst_depth_image,
                                       RHI_IMAGE_ASPECT_DEPTH_BIT,
                                       m_rhi->getSwapchainInfo().extent.width,
                                       m_rhi->getSwapchainInfo().extent.height);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.dstAccessMask =
                RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | RHI_ACCESS_SHADER_READ_BIT;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.image         = m_dst_depth_image;
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_SHADER_READ_BIT;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);
        }

        m_rhi->popEvent(m_copy_command_buffer); // end depth image copy label

        m_rhi->pushEvent(m_copy_command_buffer, "Copy Normal Image for Scan", color);

        // color image
        subresourceRange                    = {RHI_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        imagememorybarrier.subresourceRange = subresourceRange;
        {
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_UNDEFINED;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.srcAccessMask = 0;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.image         = m_dst_normal_image;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_UNDEFINED;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_READ_BIT;
            imagememorybarrier.image         = m_src_normal_image;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            m_rhi->cmdCopyImageToImage(m_copy_command_buffer,
                                       m_src_normal_image,
                                       RHI_IMAGE_ASPECT_COLOR_BIT,
                                       m_dst_normal_image,
                                       RHI_IMAGE_ASPECT_COLOR_BIT,
                                       m_rhi->getSwapchainInfo().extent.width,
                                       m_rhi->getSwapchainInfo().extent.height);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT | RHI_ACCESS_SHADER_READ_BIT;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.image         = m_dst_normal_image;
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // RHI_IMAGE_LAYOUT_GENERAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT | RHI_ACCESS_SHADER_READ_BIT;

            m_rhi->cmdPipelineBarrier(m_copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);
        }

        m_rhi->popEvent(m_copy_command_buffer);

        bool res_end_command_buffer = m_rhi->endCommandBufferPFN(m_copy_command_buffer);
        assert(RHI_SUCCESS == res_end_command_buffer);

        bool res_reset_fences = m_rhi->resetFencesPFN(1, &m_fence); // &(m_rhi->getFenceList()[index]
        assert(RHI_SUCCESS == res_reset_fences);

        RHIPipelineStageFlags wait_stages[] = {RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        RHISubmitInfo         submit_info   = {};
        submit_info.sType                   = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
        //submit_info.waitSemaphoreCount      = 1;
        //submit_info.pWaitSemaphores         = &(m_rhi->getTextureCopySemaphore(index));
        submit_info.pWaitDstStageMask       = wait_stages;
        submit_info.commandBufferCount      = 1;
        submit_info.pCommandBuffers         = &m_copy_command_buffer;
   /*     submit_info.signalSemaphoreCount    = 0;
        submit_info.pSignalSemaphores       = nullptr;*/
        bool res_queue_submit = m_rhi->queueSubmit(m_rhi->getGraphicsQueue(), 1, &submit_info, m_fence); // &(m_rhi->getFenceList()[index]
        assert(RHI_SUCCESS == res_queue_submit);

        m_rhi->queueWaitIdle(m_rhi->getGraphicsQueue());
    }

    void ScanPass::setDepthAndNormalImage(RHIImage* depth_image, RHIImage* normal_image) 
    {
        m_src_depth_image  = depth_image;
        m_src_normal_image = normal_image;
    }

    void ScanPass::draw()
    {
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Scan", color);

        m_rhi->cmdBindPipelinePFN(m_rhi->getCurrentCommandBuffer(), RHI_PIPELINE_BIND_POINT_GRAPHICS, m_render_pipelines[0].pipeline);
        m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().viewport);
        m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().scissor);
        m_rhi->cmdBindDescriptorSetsPFN(m_rhi->getCurrentCommandBuffer(),
                                        RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_render_pipelines[0].layout,
                                        0,
                                        1,
                                        &m_descriptor_infos[0].descriptor_set,
                                        0,
                                        NULL);

        m_rhi->cmdDraw(m_rhi->getCurrentCommandBuffer(), 3, 1, 0, 0);

        m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    }
} // namespace Piccolo
