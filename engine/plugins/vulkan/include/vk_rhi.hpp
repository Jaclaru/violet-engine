#pragma once

#include "vk_common.hpp"
#include "vk_resource.hpp"
#include <memory>
#include <vector>

namespace violet::vk
{
class vk_command_queue;
class vk_semaphore;
class vk_fence;
class vk_destruction_list;

class vk_rhi : public rhi_context
{
public:
    vk_rhi() noexcept;
    vk_rhi(const vk_rhi&) = delete;
    virtual ~vk_rhi();

    virtual bool initialize(const rhi_desc& desc) override;

    virtual rhi_render_command* allocate_command() override;
    virtual void execute(
        rhi_render_command* const* commands,
        std::size_t command_count,
        rhi_semaphore* const* signal_semaphores,
        std::size_t signal_semaphore_count,
        rhi_semaphore* const* wait_semaphores,
        std::size_t wait_semaphore_count,
        rhi_fence* fence) override;

    virtual void begin_frame() override;
    virtual void end_frame() override;
    virtual void present(rhi_semaphore* const* wait_semaphores, std::size_t wait_semaphore_count)
        override;

    virtual void resize(std::uint32_t width, std::uint32_t height) override;

    virtual rhi_resource* get_back_buffer() override;

    virtual rhi_fence* get_in_flight_fence() override;
    virtual rhi_semaphore* get_image_available_semaphore() override;

    vk_command_queue* get_graphics_queue() const noexcept { return m_graphics_queue.get(); }

    vk_rhi& operator=(const vk_rhi&) = delete;

public:
    virtual rhi_render_pass* make_render_pass(const rhi_render_pass_desc& desc) override;
    virtual void destroy_render_pass(rhi_render_pass* render_pass) override;

    virtual rhi_render_pipeline* make_render_pipeline(
        const rhi_render_pipeline_desc& desc) override;
    virtual void destroy_render_pipeline(rhi_render_pipeline* render_pipeline) override;

    virtual rhi_pipeline_parameter_layout* make_pipeline_parameter_layout(
        const rhi_pipeline_parameter_layout_desc& desc) override;
    virtual void destroy_pipeline_parameter_layout(
        rhi_pipeline_parameter_layout* pipeline_parameter_layout) override;

    virtual rhi_framebuffer* make_framebuffer(const rhi_framebuffer_desc& desc) override;
    virtual void destroy_framebuffer(rhi_framebuffer* framebuffer) override;

    virtual rhi_resource* make_vertex_buffer(const rhi_vertex_buffer_desc& desc) override;
    virtual rhi_resource* make_index_buffer(const rhi_index_buffer_desc& desc) override;

    virtual rhi_resource* make_texture(
        const std::uint8_t* data,
        std::uint32_t width,
        std::uint32_t height,
        rhi_resource_format format) override;
    virtual rhi_resource* make_texture(const char* file) override;
    virtual rhi_resource* make_texture_cube(
        const char* left,
        const char* right,
        const char* top,
        const char* bottom,
        const char* front,
        const char* back) override;

    virtual rhi_resource* make_shadow_map(const rhi_shadow_map_desc& desc) override;

    virtual rhi_resource* make_render_target(const rhi_render_target_desc& desc) override;
    virtual rhi_resource* make_depth_stencil_buffer(
        const rhi_depth_stencil_buffer_desc& desc) override;

    virtual rhi_fence* make_fence() override;
    virtual void destroy_fence(rhi_fence* fence) override;

    virtual rhi_semaphore* make_semaphore() override;
    virtual void destroy_semaphore(rhi_semaphore* semaphore) override;

    virtual std::size_t get_frame_resource_count() const noexcept override
    {
        return m_frame_resource_count;
    }

    virtual std::size_t get_frame_resource_index() const noexcept override
    {
        return m_frame_resource_index;
    }

public:
    VkDevice get_device() const noexcept { return m_device; }
    VkPhysicalDevice get_physical_device() const noexcept { return m_physical_device; }

private:
    struct queue_family_indices
    {
        std::uint32_t graphics;
        std::uint32_t present;
    };

    bool initialize_instance(
        const std::vector<const char*>& desired_layers,
        const std::vector<const char*>& desired_extensions);
    bool initialize_physical_device(const std::vector<const char*>& desired_extensions);
    void initialize_logic_device(const std::vector<const char*>& enabled_extensions);
    void initialize_swapchain(std::uint32_t width, std::uint32_t height);
    void initialize_sync();

    bool check_extension_support(
        const std::vector<const char*>& desired_extensions,
        const std::vector<VkExtensionProperties>& available_extensions) const;

    VkInstance m_instance;
    VkPhysicalDevice m_physical_device;
    VkDevice m_device;

    VkSurfaceKHR m_surface;

    VkSwapchainKHR m_swapchain;
    std::vector<std::unique_ptr<vk_swapchain_image>> m_swapchain_images;
    std::uint32_t m_swapchain_image_index;

    queue_family_indices m_queue_family_indices;
    std::unique_ptr<vk_command_queue> m_graphics_queue;
    std::unique_ptr<vk_command_queue> m_present_queue;

    std::size_t m_frame_count;
    std::size_t m_frame_resource_count;
    std::size_t m_frame_resource_index;

    std::vector<std::unique_ptr<vk_semaphore>> m_image_available_semaphores;
    std::vector<std::unique_ptr<vk_fence>> m_in_flight_fences;

    std::unique_ptr<vk_destruction_list> m_destruction_list;

#ifndef NDEUBG
    VkDebugUtilsMessengerEXT m_debug_messenger;
#endif
};
} // namespace violet::vk