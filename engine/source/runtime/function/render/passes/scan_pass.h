#pragma once

#include "runtime/function/render/render_pass.h"
#include "runtime/function/render/render_resource.h"


namespace Piccolo
{
    struct ScanPassInitInfo : RenderPassInitInfo
    {
        RHIRenderPass* render_pass;
        RHIImageView*  input_attachment;
    };

    class ScanPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info) override final;
        void draw() override final;

        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;
        void updateAfterFramebufferRecreate(RHIImageView* input_attachment);

    private:
        void prepareUniformBuffer();
        void setupDescriptorSetLayout();
        void setupPipelines();
        void setupDescriptorSet();

    private:
        ScanResourceData m_scan_resource_data;
        RHIBuffer*       m_uniform_buffer = nullptr;
        void*            m_uniform_buffer_mapped {nullptr};
    };
} // namespace Piccolo
