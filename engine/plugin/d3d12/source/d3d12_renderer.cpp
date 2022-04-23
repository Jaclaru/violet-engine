#include "d3d12_renderer.hpp"
#include "d3d12_context.hpp"
#include "d3d12_frame_resource.hpp"
#include "d3d12_utility.hpp"
#include <iostream>

namespace ash::graphics::d3d12
{

d3d12_swap_chain::d3d12_swap_chain(HWND handle, std::uint32_t width, std::uint32_t height)
    : d3d12_swap_chain(handle, width, height, 1)
{
}

d3d12_swap_chain::d3d12_swap_chain(
    HWND handle,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t multiple_sampling)
{
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
    swap_chain_desc.Width = width;
    swap_chain_desc.Height = height;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.Format = RENDER_TARGET_FORMAT;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    // Multisampling a back buffer is not supported in D3D12.
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;

    throw_if_failed(d3d12_context::factory()->CreateSwapChainForHwnd(
        d3d12_context::command()->command_queue(),
        handle,
        &swap_chain_desc,
        nullptr,
        nullptr,
        &m_swap_chain));

    for (UINT i = 0; i < 2; ++i)
    {
        d3d12_ptr<D3D12Resource> buffer;
        m_swap_chain->GetBuffer(i, IID_PPV_ARGS(&buffer));
        m_back_buffer.push_back(std::make_unique<d3d12_back_buffer>(buffer));
    }

    // Create a depth stencil buffer and view.
    m_depth_stencil_buffer = std::make_unique<d3d12_depth_stencil_buffer>(
        width,
        height,
        DEPTH_STENCIL_FORMAT,
        multiple_sampling);

    // Query render target sample level.
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS sample_level = {};
    sample_level.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    sample_level.Format = RENDER_TARGET_FORMAT;
    sample_level.NumQualityLevels = 0;
    sample_level.SampleCount = static_cast<UINT>(multiple_sampling);
    throw_if_failed(d3d12_context::device()->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &sample_level,
        sizeof(sample_level)));
    m_sample_desc.Count = sample_level.SampleCount;
    m_sample_desc.Quality = sample_level.NumQualityLevels - 1;
}

void d3d12_swap_chain::begin_frame(D3D12GraphicsCommandList* command_list)
{
    m_back_buffer[back_buffer_index()]->transition_state(
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        command_list);
}

void d3d12_swap_chain::end_frame(D3D12GraphicsCommandList* command_list)
{
    m_back_buffer[back_buffer_index()]->transition_state(
        D3D12_RESOURCE_STATE_PRESENT,
        command_list);
}

void d3d12_swap_chain::present()
{
    throw_if_failed(m_swap_chain->Present(0, 0));
}

d3d12_resource* d3d12_swap_chain::back_buffer()
{
    return m_back_buffer[back_buffer_index()].get();
}

d3d12_resource* d3d12_swap_chain::depth_stencil()
{
    return m_depth_stencil_buffer.get();
}

D3D12_CPU_DESCRIPTOR_HANDLE d3d12_swap_chain::render_target_handle()
{
    return m_back_buffer[back_buffer_index()]->cpu_handle();
}

D3D12_CPU_DESCRIPTOR_HANDLE d3d12_swap_chain::depth_stencil_handle()
{
    return m_depth_stencil_buffer->cpu_handle();
}

UINT64 d3d12_swap_chain::back_buffer_index() const noexcept
{
    return d3d12_frame_counter::frame_counter() % 2;
}

d3d12_multisampling_swap_chain::d3d12_multisampling_swap_chain(
    HWND handle,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t multiple_sampling)
    : d3d12_swap_chain(handle, width, height, multiple_sampling)
{
    // Create a multisampled render target.
    m_render_target = std::make_unique<d3d12_render_target>(
        width,
        height,
        RENDER_TARGET_FORMAT,
        multiple_sampling);
}

void d3d12_multisampling_swap_chain::begin_frame(D3D12GraphicsCommandList* command_list)
{
    m_render_target->transition_state(D3D12_RESOURCE_STATE_RENDER_TARGET, command_list);
}

void d3d12_multisampling_swap_chain::end_frame(D3D12GraphicsCommandList* command_list)
{
    d3d12_resource::transition_list list = {
        {m_render_target.get(),                    D3D12_RESOURCE_STATE_RESOLVE_SOURCE},
        {m_back_buffer[back_buffer_index()].get(), D3D12_RESOURCE_STATE_RESOLVE_DEST  }
    };
    d3d12_resource::transition_state(list, command_list);

    command_list->ResolveSubresource(
        m_back_buffer[back_buffer_index()]->resource(),
        0,
        m_render_target->resource(),
        0,
        RENDER_TARGET_FORMAT);

    m_back_buffer[back_buffer_index()]->transition_state(
        D3D12_RESOURCE_STATE_PRESENT,
        command_list);
}

d3d12_resource* d3d12_multisampling_swap_chain::back_buffer()
{
    return m_render_target.get();
}

D3D12_CPU_DESCRIPTOR_HANDLE d3d12_multisampling_swap_chain::render_target_handle()
{
    return m_render_target->cpu_handle();
}

d3d12_renderer::d3d12_renderer(
    HWND handle,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t multiple_sampling,
    D3D12GraphicsCommandList* command_list)
{
    auto factory = d3d12_context::factory();
    auto device = d3d12_context::device();

    // Get adapter information.
    d3d12_ptr<DXGIAdapter> adapter;
    for (UINT i = 0;; ++i)
    {
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        m_adapter_info.push_back(wstring_to_string(desc.Description));
        adapter.Reset();
    }

    if (multiple_sampling == 1)
    {
        m_swap_chain = std::make_unique<d3d12_swap_chain>(handle, width, height);
    }
    else
    {
        m_swap_chain = std::make_unique<d3d12_multisampling_swap_chain>(
            handle,
            width,
            height,
            multiple_sampling);
    }

    m_viewport.Width = static_cast<float>(width);
    m_viewport.Height = static_cast<float>(height);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;

    m_scissor_rect.top = 0;
    m_scissor_rect.bottom = height;
    m_scissor_rect.left = 0;
    m_scissor_rect.right = width;
}

void d3d12_renderer::begin_frame()
{
    d3d12_context::begin_frame();
}

void d3d12_renderer::end_frame()
{
    d3d12_context::end_frame();
}

render_command* d3d12_renderer::allocate_command()
{
    d3d12_render_command* command =
        d3d12_context::command()->allocate_render_command(d3d12_render_command_type::RENDER);

    D3D12GraphicsCommandList* command_list = command->get();

    D3D12DescriptorHeap* heaps[] = {
        d3d12_context::resource()->visible_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)->heap()};
    command_list->SetDescriptorHeaps(1, heaps);

    command_list->RSSetViewports(1, &m_viewport);
    command_list->RSSetScissorRects(1, &m_scissor_rect);

    auto render_target_handle = m_swap_chain->render_target_handle();
    auto depth_stencil_buffer_handle = m_swap_chain->depth_stencil_handle();
    command_list->OMSetRenderTargets(1, &render_target_handle, true, &depth_stencil_buffer_handle);

    return command;
}

void d3d12_renderer::execute(render_command* command)
{
    d3d12_render_command* c = static_cast<d3d12_render_command*>(command);
    d3d12_context::command()->execute_command(c);
}

resource* d3d12_renderer::back_buffer()
{
    return m_swap_chain->back_buffer();
}

resource* d3d12_renderer::depth_stencil()
{
    return m_swap_chain->depth_stencil();
}

std::size_t d3d12_renderer::adapter(adapter_info* infos, std::size_t size) const
{
    std::size_t i = 0;
    for (; i < size && i < m_adapter_info.size(); ++i)
    {
        memcpy(infos[i].description, m_adapter_info[i].c_str(), m_adapter_info[i].size());
    }

    return i;
}

void d3d12_renderer::begin_frame(D3D12GraphicsCommandList* command_list)
{
    m_swap_chain->begin_frame(command_list);

    static const float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};
    command_list->ClearRenderTargetView(
        m_swap_chain->render_target_handle(),
        clear_color,
        1,
        &m_scissor_rect);
    command_list->ClearDepthStencilView(
        m_swap_chain->depth_stencil_handle(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f,
        0,
        0,
        nullptr);
}

void d3d12_renderer::end_frame(D3D12GraphicsCommandList* command_list)
{
    m_swap_chain->end_frame(command_list);
}

void d3d12_renderer::present()
{
    m_swap_chain->present();
}
} // namespace ash::graphics::d3d12