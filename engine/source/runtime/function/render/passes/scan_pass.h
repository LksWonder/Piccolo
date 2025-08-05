#pragma once

#include "runtime/function/render/render_pass.h"
#include "runtime/function/render/render_resource.h"


namespace Piccolo
{
    struct ScanPassInitInfo : RenderPassInitInfo
    {
        RHIRenderPass* render_pass;
        RHIImageView*  input_attachment;
        RHIImageView*  depth_input_attachment;
    };

    class ScanPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info) override final;
        void draw() override final;

        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;
        void updateAfterFramebufferRecreate(RHIImageView* input_attachment, RHIImageView* depth_attachment);
        
        void createCommandBuffer();

        void recreateImageView();

        void copyNormalAndDepthImage();

        void setDepthAndNormalImage(RHIImage* depth_image, RHIImage* normal_image);

    private:
        void prepareUniformBuffer();
        void setupDescriptorSetLayout();
        void setupPipelines();
        void setupDescriptorSet();

    private:
        ScanResourceData m_scan_resource_data;
        RHIBuffer*       m_uniform_buffer = nullptr;
        void*            m_uniform_buffer_mapped {nullptr};

        RHICommandBuffer* m_copy_command_buffer = nullptr;
        RHIFence* m_fence = nullptr;

        RHIImage*        m_src_depth_image         = nullptr;
        RHIImage*        m_dst_normal_image        = nullptr;
        RHIImage*        m_src_normal_image        = nullptr;
        RHIImage*        m_dst_depth_image         = nullptr;
        RHIImageView*    m_src_depth_image_view    = nullptr;
        RHIImageView*    m_src_normal_image_view   = nullptr;
        RHIDeviceMemory* m_dst_normal_image_memory = nullptr;
        RHIDeviceMemory* m_dst_depth_image_memory  = nullptr;
    };
} // namespace Piccolo
