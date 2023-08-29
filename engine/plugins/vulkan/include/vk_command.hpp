#pragma once

#include "vk_common.hpp"
#include "vk_sync.hpp"
#include <memory>
#include <vector>

namespace violet::vk
{
class vk_rhi;
class vk_command : public rhi_render_command
{
public:
    vk_command(VkCommandBuffer command_buffer, vk_rhi* rhi) noexcept;
    virtual ~vk_command();

    VkCommandBuffer get_command_buffer() const noexcept { return m_command_buffer; }

public:
    virtual void begin(rhi_render_pass* render_pass, rhi_framebuffer* framebuffer) override;
    virtual void end() override;
    virtual void next() override;

    virtual void set_pipeline(rhi_render_pipeline* render_pipeline) override;
    virtual void set_parameter(std::size_t index, rhi_pipeline_parameter* pipeline_parameter)
        override;

    virtual void set_viewport(const rhi_viewport& viewport) override;
    virtual void set_scissor(const rhi_scissor_rect* rects, std::size_t size) override;

    virtual void set_vertex_buffers(
        rhi_resource* const* vertex_buffers,
        std::size_t vertex_buffer_count) override;
    virtual void set_index_buffer(rhi_resource* index_buffer) override;

    virtual void draw(std::size_t vertex_start, std::size_t vertex_count) override;
    virtual void draw_indexed(
        std::size_t index_start,
        std::size_t index_count,
        std::size_t vertex_base) override;

    virtual void clear_render_target(rhi_resource* render_target, const float4& color) override;
    virtual void clear_depth_stencil(
        rhi_resource* depth_stencil,
        bool clear_depth,
        float depth,
        bool clear_stencil,
        std::uint8_t stencil) override;

    void reset();

private:
    VkCommandBuffer m_command_buffer;

    VkRenderPass m_current_render_pass;
    VkPipelineLayout m_current_pipeline_layout;

    vk_rhi* m_rhi;
};

class vk_graphics_queue
{
public:
    vk_graphics_queue(std::uint32_t queue_family_index, vk_rhi* rhi);
    ~vk_graphics_queue();

    vk_command* allocate_command();

    void execute(
        rhi_render_command* const* commands,
        std::size_t command_count,
        rhi_semaphore* const* signal_semaphores,
        std::size_t signal_semaphore_count,
        rhi_semaphore* const* wait_semaphores,
        std::size_t wait_semaphore_count,
        rhi_fence* fence);
    void execute_sync(rhi_render_command* command);

    void begin_frame();

    VkQueue get_queue() const noexcept { return m_queue; }

private:
    VkQueue m_queue;

    VkCommandPool m_command_pool;
    std::vector<std::vector<vk_command*>> m_active_commands;
    std::vector<vk_command*> m_free_commands;
    std::vector<std::unique_ptr<vk_command>> m_commands;

    std::unique_ptr<vk_fence> m_fence;
    vk_rhi* m_rhi;
};

class vk_present_queue
{
public:
    vk_present_queue(std::uint32_t queue_family_index, vk_rhi* rhi);
    ~vk_present_queue();

    void present(
        VkSwapchainKHR swapchain,
        std::uint32_t image_index,
        rhi_semaphore* const* wait_semaphores,
        std::size_t wait_semaphore_count);

    VkQueue get_queue() const noexcept { return m_queue; }

private:
    VkQueue m_queue;
};
} // namespace violet::vk