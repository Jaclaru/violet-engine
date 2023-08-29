#include "vk_pipeline.hpp"
#include "vk_render_pass.hpp"
#include "vk_resource.hpp"
#include "vk_rhi.hpp"
#include "vk_util.hpp"
#include <fstream>

namespace violet::vk
{
namespace
{
std::uint32_t get_vertex_attribute_stride(rhi_resource_format format) noexcept
{
    switch (format)
    {
    case RHI_RESOURCE_FORMAT_R8_UNORM:
    case RHI_RESOURCE_FORMAT_R8_SNORM:
    case RHI_RESOURCE_FORMAT_R8_UINT:
    case RHI_RESOURCE_FORMAT_R8_SINT:
        return 1;
    case RHI_RESOURCE_FORMAT_R8G8_UNORM:
    case RHI_RESOURCE_FORMAT_R8G8_SNORM:
    case RHI_RESOURCE_FORMAT_R8G8_UINT:
    case RHI_RESOURCE_FORMAT_R8G8_SINT:
        return 2;
    case RHI_RESOURCE_FORMAT_R8G8B8_UNORM:
    case RHI_RESOURCE_FORMAT_R8G8B8_SNORM:
    case RHI_RESOURCE_FORMAT_R8G8B8_UINT:
    case RHI_RESOURCE_FORMAT_R8G8B8_SINT:
        return 3;
    case RHI_RESOURCE_FORMAT_R8G8B8A8_UNORM:
    case RHI_RESOURCE_FORMAT_R8G8B8A8_SNORM:
    case RHI_RESOURCE_FORMAT_R8G8B8A8_UINT:
    case RHI_RESOURCE_FORMAT_R8G8B8A8_SINT:
        return 4;
    case RHI_RESOURCE_FORMAT_R32_UINT:
    case RHI_RESOURCE_FORMAT_R32_SINT:
    case RHI_RESOURCE_FORMAT_R32_FLOAT:
        return 4;
    case RHI_RESOURCE_FORMAT_R32G32_UINT:
    case RHI_RESOURCE_FORMAT_R32G32_SINT:
    case RHI_RESOURCE_FORMAT_R32G32_FLOAT:
        return 8;
    case RHI_RESOURCE_FORMAT_R32G32B32_UINT:
    case RHI_RESOURCE_FORMAT_R32G32B32_SINT:
    case RHI_RESOURCE_FORMAT_R32G32B32_FLOAT:
        return 12;
    case RHI_RESOURCE_FORMAT_R32G32B32A32_UINT:
    case RHI_RESOURCE_FORMAT_R32G32B32A32_SINT:
    case RHI_RESOURCE_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    default:
        return 0;
    }
}
} // namespace

vk_pipeline_parameter_layout::vk_pipeline_parameter_layout(
    const rhi_pipeline_parameter_layout_desc& desc,
    vk_rhi* rhi)
    : m_rhi(rhi)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    m_parameter_infos.resize(desc.parameter_count);
    std::size_t uniform_buffer_count = 0;

    for (std::size_t i = 0; i < desc.parameter_count; ++i)
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = static_cast<std::uint32_t>(i);

        switch (desc.parameters[i].type)
        {
        case RHI_PIPELINE_PARAMETER_TYPE_UNIFORM_BUFFER: {
            binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            binding.pImmutableSamplers = nullptr;

            m_parameter_infos[i].index = uniform_buffer_count;
            m_parameter_infos[i].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            m_parameter_infos[i].uniform_buffer.size = desc.parameters[i].size;

            ++uniform_buffer_count;

            break;
        }
        default:
            break;
        }

        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {};
    descriptor_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_info.pBindings = bindings.data();
    descriptor_set_layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());

    throw_if_failed(vkCreateDescriptorSetLayout(
        m_rhi->get_device(),
        &descriptor_set_layout_info,
        nullptr,
        &m_layout));
}

vk_pipeline_parameter_layout::~vk_pipeline_parameter_layout()
{
    vkDestroyDescriptorSetLayout(m_rhi->get_device(), m_layout, nullptr);
}

vk_pipeline_parameter::vk_pipeline_parameter(vk_pipeline_parameter_layout* layout, vk_rhi* rhi)
    : m_layout(layout),
      m_last_update_frame(0),
      m_last_sync_frame(-1),
      m_rhi(rhi)
{
    auto& parameter_infos = m_layout->get_parameter_infos();
    for (auto& parameter_info : parameter_infos)
    {
        switch (parameter_info.type)
        {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
            auto uniform_buffer = std::make_unique<vk_uniform_buffer>(
                nullptr,
                parameter_info.uniform_buffer.size * rhi->get_frame_resource_count(),
                m_rhi);

            m_uniform_buffers.push_back(std::move(uniform_buffer));
            break;
        }

        default:
            throw vk_exception("Invalid parameter type.");
        }
    }
    m_parameter_update_frame.resize(parameter_infos.size());

    m_descriptor_sets.resize(rhi->get_frame_resource_count());
    for (std::size_t i = 0; i < m_descriptor_sets.size(); ++i)
    {
        m_descriptor_sets[i] = rhi->allocate_descriptor_set(layout->get_layout());

        std::vector<VkWriteDescriptorSet> descriptor_write;
        std::vector<VkDescriptorBufferInfo> buffer_infos;
        buffer_infos.reserve(parameter_infos.size());

        for (std::size_t j = 0; j < parameter_infos.size(); ++j)
        {
            if (parameter_infos[j].type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                continue;

            VkDescriptorBufferInfo info = {};
            info.buffer = m_uniform_buffers[parameter_infos[j].index]->get_buffer_handle();
            info.offset = parameter_infos[j].uniform_buffer.size * i;
            info.range = parameter_infos[j].uniform_buffer.size;
            buffer_infos.push_back(info);

            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_descriptor_sets[i];
            write.dstBinding = static_cast<std::uint32_t>(j);
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &buffer_infos.back();
            write.pImageInfo = nullptr;
            write.pTexelBufferView = nullptr;

            descriptor_write.push_back(write);
        }

        if (!descriptor_write.empty())
        {
            vkUpdateDescriptorSets(
                m_rhi->get_device(),
                static_cast<std::uint32_t>(descriptor_write.size()),
                descriptor_write.data(),
                0,
                nullptr);
        }
    }
}

vk_pipeline_parameter::~vk_pipeline_parameter()
{
}

void vk_pipeline_parameter::set(
    std::size_t index,
    const void* data,
    std::size_t size,
    std::size_t offset)
{
    sync();

    std::size_t frame_resource_index = m_rhi->get_frame_resource_index();

    auto& parameter_info = m_layout->get_parameter_infos()[index];
    assert(parameter_info.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

    void* buffer = m_uniform_buffers[parameter_info.index]->get_buffer();
    void* target = static_cast<std::uint8_t*>(buffer) + offset +
                   parameter_info.uniform_buffer.size * frame_resource_index;

    std::memcpy(target, data, size);

    m_last_update_frame = m_parameter_update_frame[index] = m_rhi->get_frame_count();
}

void vk_pipeline_parameter::set(std::size_t index, rhi_resource* texture)
{
}

VkDescriptorSet vk_pipeline_parameter::get_descriptor_set() const noexcept
{
    return m_descriptor_sets[m_rhi->get_frame_resource_index()];
}

void vk_pipeline_parameter::sync()
{
    std::size_t corrent_frame = m_rhi->get_frame_count();
    std::size_t frame_resource_count = m_rhi->get_frame_resource_count();
    if (m_last_sync_frame == corrent_frame ||
        m_last_update_frame + frame_resource_count < corrent_frame)
        return;
    m_last_sync_frame = corrent_frame;

    std::size_t current_index = m_rhi->get_frame_resource_index();
    std::size_t previous_index = (current_index + frame_resource_count - 1) % frame_resource_count;

    for (std::size_t i = 0; i < m_parameter_update_frame.size(); ++i)
    {
        if (m_parameter_update_frame[i] + frame_resource_count < corrent_frame)
            continue;

        auto& parameter_info = m_layout->get_parameter_infos()[i];
        if (parameter_info.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            void* buffer = m_uniform_buffers[parameter_info.index]->get_buffer();
            std::size_t size = parameter_info.uniform_buffer.size;

            void* source = static_cast<std::uint8_t*>(buffer) + previous_index * size;
            void* target = static_cast<std::uint8_t*>(buffer) + current_index * size;

            std::memcpy(target, source, size);
        }
    }
}

vk_render_pipeline::vk_render_pipeline(
    const rhi_render_pipeline_desc& desc,
    VkExtent2D extent,
    vk_rhi* rhi)
    : m_rhi(rhi)
{
    VkPipelineShaderStageCreateInfo vert_shader_stage_info = {};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = load_shader(desc.vertex_shader);
    vert_shader_stage_info.pName = "vs_main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info = {};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = load_shader(desc.pixel_shader);
    frag_shader_stage_info.pName = "ps_main";

    VkPipelineShaderStageCreateInfo shader_stage_infos[] = {
        vert_shader_stage_info,
        frag_shader_stage_info};

    std::vector<VkVertexInputBindingDescription> binding_descriptions;
    std::vector<VkVertexInputAttributeDescription> attribute_descriptions;
    for (std::size_t i = 0; i < desc.vertex_attribute_count; ++i)
    {
        VkVertexInputBindingDescription binding = {};
        binding.binding = static_cast<std::uint32_t>(i);
        binding.stride = get_vertex_attribute_stride(desc.vertex_attributes[i].format);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binding_descriptions.push_back(binding);

        VkVertexInputAttributeDescription attribute = {};
        attribute.binding = binding.binding;
        attribute.format = vk_util::map_format(desc.vertex_attributes[i].format);
        attribute.location = static_cast<std::uint32_t>(i);
        attribute_descriptions.push_back(attribute);
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_state_info = {};
    vertex_input_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_info.pVertexAttributeDescriptions = attribute_descriptions.data();
    vertex_input_state_info.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(attribute_descriptions.size());
    vertex_input_state_info.pVertexBindingDescriptions = binding_descriptions.data();
    vertex_input_state_info.vertexBindingDescriptionCount =
        static_cast<std::uint32_t>(binding_descriptions.size());

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_info = {};
    input_assembly_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state_info.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = extent.width;
    viewport.height = extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = 1;
    viewport_info.pViewports = &viewport;
    viewport_info.scissorCount = 1;
    viewport_info.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state_info = {};
    rasterization_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_info.depthClampEnable = VK_FALSE;
    rasterization_state_info.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state_info.lineWidth = 1.0f;
    rasterization_state_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization_state_info.depthBiasEnable = VK_FALSE;
    rasterization_state_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_info.depthBiasClamp = 0.0f;
    rasterization_state_info.depthBiasSlopeFactor = 0.0f;
    switch (desc.rasterizer.cull_mode)
    {
    case RHI_CULL_MODE_NONE:
        rasterization_state_info.cullMode = VK_CULL_MODE_NONE;
        break;
    case RHI_CULL_MODE_FRONT:
        rasterization_state_info.cullMode = VK_CULL_MODE_FRONT_BIT;
        break;
    case RHI_CULL_MODE_BACK:
        rasterization_state_info.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    default:
        throw vk_exception("Invalid cull mode.");
    }

    VkPipelineMultisampleStateCreateInfo multisample_state_info = {};
    multisample_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_info.sampleShadingEnable = VK_FALSE;
    multisample_state_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample_state_info.minSampleShading = 1.0f;
    multisample_state_info.pSampleMask = nullptr;
    multisample_state_info.alphaToCoverageEnable = VK_FALSE;
    multisample_state_info.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_info = {};
    depth_stencil_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state_info.depthTestEnable = VK_TRUE;
    depth_stencil_state_info.depthWriteEnable = VK_TRUE;
    depth_stencil_state_info.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil_state_info.stencilTestEnable = VK_FALSE;
    depth_stencil_state_info.front = {};
    depth_stencil_state_info.back = {};

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blend_info = {};
    color_blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_info.logicOpEnable = VK_FALSE;
    color_blend_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_info.attachmentCount = 1;
    color_blend_info.pAttachments = &color_blend_attachment;
    color_blend_info.blendConstants[0] = 0.0f;
    color_blend_info.blendConstants[1] = 0.0f;
    color_blend_info.blendConstants[2] = 0.0f;
    color_blend_info.blendConstants[3] = 0.0f;

    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    for (std::size_t i = 0; i < desc.parameter_count; ++i)
    {
        descriptor_set_layouts.push_back(
            static_cast<vk_pipeline_parameter_layout*>(desc.parameters[i])->get_layout());
    }

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pSetLayouts = descriptor_set_layouts.data();
    layout_info.setLayoutCount = static_cast<std::uint32_t>(descriptor_set_layouts.size());
    throw_if_failed(
        vkCreatePipelineLayout(m_rhi->get_device(), &layout_info, nullptr, &m_pipeline_layout));

    std::vector<VkDynamicState> dynamic_state = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS};

    VkPipelineDynamicStateCreateInfo dynamic_state_info = {};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.pDynamicStates = dynamic_state.data();
    dynamic_state_info.dynamicStateCount = static_cast<std::uint32_t>(dynamic_state.size());

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stage_infos;
    pipeline_info.pVertexInputState = &vertex_input_state_info;
    pipeline_info.pInputAssemblyState = &input_assembly_state_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pRasterizationState = &rasterization_state_info;
    pipeline_info.pMultisampleState = &multisample_state_info;
    pipeline_info.pColorBlendState = &color_blend_info;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = m_pipeline_layout;
    pipeline_info.renderPass = static_cast<vk_render_pass*>(desc.render_pass)->get_render_pass();
    pipeline_info.subpass = static_cast<std::uint32_t>(desc.render_subpass_index);
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.pDepthStencilState = &depth_stencil_state_info;

    throw_if_failed(vkCreateGraphicsPipelines(
        m_rhi->get_device(),
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &m_pipeline));

    vkDestroyShaderModule(m_rhi->get_device(), vert_shader_stage_info.module, nullptr);
    vkDestroyShaderModule(m_rhi->get_device(), frag_shader_stage_info.module, nullptr);
}

vk_render_pipeline::~vk_render_pipeline()
{
    vkDestroyPipeline(m_rhi->get_device(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_rhi->get_device(), m_pipeline_layout, nullptr);
}

VkShaderModule vk_render_pipeline::load_shader(std::string_view path)
{
    std::ifstream fin(path.data(), std::ios::binary);
    if (!fin.is_open())
        throw vk_exception("Failed to open file!");

    fin.seekg(0, std::ios::end);
    std::size_t buffer_size = fin.tellg();

    std::vector<char> buffer(buffer_size);
    fin.seekg(0);
    fin.read(buffer.data(), buffer_size);
    fin.close();

    VkShaderModuleCreateInfo shader_module_info = {};
    shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_info.pCode = reinterpret_cast<std::uint32_t*>(buffer.data());
    shader_module_info.codeSize = buffer.size();

    VkShaderModule result = VK_NULL_HANDLE;
    throw_if_failed(
        vkCreateShaderModule(m_rhi->get_device(), &shader_module_info, nullptr, &result));
    return result;
}
} // namespace violet::vk