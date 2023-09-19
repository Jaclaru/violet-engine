#include "graphics/render_graph/render_pass.hpp"
#include "common/log.hpp"
#include "graphics/render_graph/render_pipeline.hpp"
#include <cassert>

namespace violet
{
render_attachment::render_attachment(std::size_t index) noexcept : m_desc{}, m_index(index)
{
}

void render_attachment::set_format(rhi_resource_format format) noexcept
{
    m_desc.format = format;
}

void render_attachment::set_load_op(rhi_attachment_load_op op) noexcept
{
    m_desc.load_op = op;
}

void render_attachment::set_store_op(rhi_attachment_store_op op) noexcept
{
    m_desc.store_op = op;
}

void render_attachment::set_stencil_load_op(rhi_attachment_load_op op) noexcept
{
    m_desc.stencil_load_op = op;
}

void render_attachment::set_stencil_store_op(rhi_attachment_store_op op) noexcept
{
    m_desc.stencil_store_op = op;
}

void render_attachment::set_initial_state(rhi_resource_state state) noexcept
{
    m_desc.initial_state = state;
}

void render_attachment::set_final_state(rhi_resource_state state) noexcept
{
    m_desc.final_state = state;
}

render_subpass::render_subpass(
    std::string_view name,
    rhi_context* rhi,
    render_pass* render_pass,
    std::size_t index)
    : render_node(name, rhi),
      m_desc{},
      m_render_pass(render_pass),
      m_index(index)
{
}

void render_subpass::add_reference(
    render_attachment* attachment,
    rhi_attachment_reference_type type,
    rhi_resource_state state)
{
    auto& desc = m_desc.references[m_desc.reference_count];
    desc.type = type;
    desc.state = state;
    desc.index = attachment->get_index();
    desc.resolve_index = 0;

    ++m_desc.reference_count;
}

void render_subpass::add_reference(
    render_attachment* attachment,
    rhi_attachment_reference_type type,
    rhi_resource_state state,
    render_attachment* resolve)
{
    auto& desc = m_desc.references[m_desc.reference_count];
    desc.type = type;
    desc.state = state;
    desc.index = attachment->get_index();
    desc.resolve_index = resolve->get_index();

    ++m_desc.reference_count;
}

render_pipeline* render_subpass::add_pipeline(std::string_view name)
{
    auto pipeline = std::make_unique<render_pipeline>(name, get_rhi());
    m_pipelines.push_back(std::move(pipeline));
    return m_pipelines.back().get();
}

bool render_subpass::compile()
{
    for (auto& pipeline : m_pipelines)
    {
        if (!pipeline->compile(m_render_pass->get_interface(), m_index))
            return false;
    }

    return true;
}

void render_subpass::execute(rhi_render_command* command, rhi_pipeline_parameter* camera_parameter)
{
    for (auto& pipeline : m_pipelines)
    {
        pipeline->set_camera_parameter(camera_parameter);
        pipeline->execute(command);
    }
}

render_pass::render_pass(std::string_view name, rhi_context* rhi)
    : render_node(name, rhi),
      m_interface(nullptr)
{
}

render_pass::~render_pass()
{
    rhi_context* rhi = get_rhi();

    if (m_interface != nullptr)
        rhi->destroy_render_pass(m_interface);
}

render_attachment* render_pass::add_attachment(std::string_view name)
{
    auto attachment = std::make_unique<render_attachment>(m_attachments.size());
    m_attachments.push_back(std::move(attachment));
    return m_attachments.back().get();
}

render_subpass* render_pass::add_subpass(std::string_view name)
{
    auto subpass = std::make_unique<render_subpass>(name, get_rhi(), this, m_subpasses.size());
    m_subpasses.push_back(std::move(subpass));
    return m_subpasses.back().get();
}

void render_pass::add_dependency(
    std::size_t source_index,
    rhi_pipeline_stage_flags source_stage,
    rhi_access_flags source_access,
    std::size_t target_index,
    rhi_pipeline_stage_flags target_stage,
    rhi_access_flags target_access)
{
    rhi_render_subpass_dependency_desc dependency = {};
    dependency.source = source_index;
    dependency.source_stage = source_stage;
    dependency.source_access = source_access;
    dependency.target = target_index;
    dependency.target_stage = target_stage;
    dependency.target_access = target_access;
    m_dependencies.push_back(dependency);
}

void render_pass::add_camera(
    rhi_scissor_rect scissor,
    rhi_viewport viewport,
    rhi_pipeline_parameter* parameter,
    rhi_framebuffer* framebuffer)
{
    return m_cameras.push_back({scissor, viewport, parameter, framebuffer});
}

bool render_pass::compile()
{
    rhi_render_pass_desc desc = {};

    for (std::size_t i = 0; i < m_attachments.size(); ++i)
        desc.attachments[i] = m_attachments[i]->get_desc();
    desc.attachment_count = m_attachments.size();

    for (std::size_t i = 0; i < m_subpasses.size(); ++i)
        desc.subpasses[i] = m_subpasses[i]->get_desc();
    desc.subpass_count = m_subpasses.size();

    for (std::size_t i = 0; i < m_dependencies.size(); ++i)
        desc.dependencies[i] = m_dependencies[i];
    desc.dependency_count = m_dependencies.size();

    m_interface = get_rhi()->create_render_pass(desc);

    if (m_interface == nullptr)
        return false;

    for (auto& subpass : m_subpasses)
    {
        if (!subpass->compile())
            return false;
    }

    return true;
}

void render_pass::execute(rhi_render_command* command)
{
    assert(!m_attachments.empty());

    for (auto& camera : m_cameras)
    {
        command->begin(m_interface, camera.framebuffer);
        command->set_scissor(&camera.scissor, 1);
        command->set_viewport(camera.viewport);

        for (std::size_t i = 0; i < m_subpasses.size(); ++i)
        {
            m_subpasses[i]->execute(command, camera.parameter);
            if (i != m_subpasses.size() - 1)
                command->next();
        }

        command->end();
    }

    m_cameras.clear();
}
} // namespace violet