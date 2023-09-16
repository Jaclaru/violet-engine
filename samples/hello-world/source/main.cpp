#include "components/mesh.hpp"
#include "components/transform.hpp"
#include "core/engine.hpp"
#include "core/node/node.hpp"
#include "graphics/graphics_system.hpp"
#include "window/window_system.hpp"
#include <filesystem>
#include <fstream>
#include <thread>

namespace violet::sample
{
class hello_world : public engine_system
{
public:
    hello_world()
        : engine_system("hello_world"),
          m_texture(nullptr),
          m_sampler(nullptr),
          m_depth_stencil(nullptr)
    {
    }

    virtual bool initialize(const dictionary& config)
    {
        log::info(config["text"]);

        auto& window = engine::get_system<window_system>();
        window.on_resize().then(
            [this](std::uint32_t width, std::uint32_t height)
            {
                log::info("Window resize: {} {}", width, height);
                resize(width, height);
            });

        engine::on_tick().then(
            [this](float delta)
            {
                tick(delta);
                engine::get_system<graphics_system>().render(m_render_graph.get());
            });

        initialize_render();

        m_test_object = std::make_unique<node>("test", engine::get_world());
        auto [mesh_handle, transform_handle] = m_test_object->add_component<mesh, transform>();
        mesh_handle->set_geometry(m_geometry.get());
        mesh_handle->add_submesh(0, 0, 12, m_material);

        return true;
    }

    virtual void shutdown()
    {
        m_render_graph = nullptr;
        rhi_context* rhi = engine::get_system<graphics_system>().get_rhi();

        m_geometry = nullptr;

        rhi->destroy_texture(m_texture);
        rhi->destroy_sampler(m_sampler);

        rhi->destroy_depth_stencil_buffer(m_depth_stencil);
    }

private:
    void initialize_render()
    {
        auto& graphics = engine::get_system<graphics_system>();
        auto& window = engine::get_system<window_system>();
        auto extent = window.get_extent();

        m_render_graph = std::make_unique<render_graph>(graphics.get_rhi());

        render_resource& back_buffer = m_render_graph->add_resource("back buffer", true);
        render_resource& depth_stencil_buffer =
            m_render_graph->add_resource("depth stencil buffer");
        depth_stencil_buffer.set_format(RHI_RESOURCE_FORMAT_D24_UNORM_S8_UINT);

        resize(extent.width, extent.height);

        render_pass& main = m_render_graph->add_render_pass("main");

        render_attachment& output_attachment = main.add_attachment("output", back_buffer);
        output_attachment.set_initial_state(RHI_RESOURCE_STATE_UNDEFINED);
        output_attachment.set_final_state(RHI_RESOURCE_STATE_PRESENT);
        output_attachment.set_load_op(RHI_ATTACHMENT_LOAD_OP_CLEAR);
        output_attachment.set_store_op(RHI_ATTACHMENT_STORE_OP_STORE);

        render_attachment& depth_stencil_attachment =
            main.add_attachment("depth stencil", depth_stencil_buffer);
        depth_stencil_attachment.set_initial_state(RHI_RESOURCE_STATE_UNDEFINED);
        depth_stencil_attachment.set_final_state(RHI_RESOURCE_STATE_DEPTH_STENCIL);
        depth_stencil_attachment.set_load_op(RHI_ATTACHMENT_LOAD_OP_CLEAR);
        depth_stencil_attachment.set_store_op(RHI_ATTACHMENT_STORE_OP_DONT_CARE);
        depth_stencil_attachment.set_stencil_load_op(RHI_ATTACHMENT_LOAD_OP_CLEAR);
        depth_stencil_attachment.set_stencil_store_op(RHI_ATTACHMENT_STORE_OP_DONT_CARE);

        render_subpass& color_pass = main.add_subpass("color");
        color_pass.add_reference(
            output_attachment,
            RHI_ATTACHMENT_REFERENCE_TYPE_COLOR,
            RHI_RESOURCE_STATE_RENDER_TARGET);
        color_pass.add_reference(
            depth_stencil_attachment,
            RHI_ATTACHMENT_REFERENCE_TYPE_DEPTH_STENCIL,
            RHI_RESOURCE_STATE_DEPTH_STENCIL);

        render_pipeline& pipeline = color_pass.add_pipeline("color");
        pipeline.set_shader(
            "hello-world/shaders/base.vert.spv",
            "hello-world/shaders/base.frag.spv");
        pipeline.set_vertex_layout({
            {"position", RHI_RESOURCE_FORMAT_R32G32B32_FLOAT},
            {"color",    RHI_RESOURCE_FORMAT_R32G32B32_FLOAT},
            {"uv",       RHI_RESOURCE_FORMAT_R32G32_FLOAT   }
        });
        pipeline.set_cull_mode(RHI_CULL_MODE_NONE);

        pipeline.set_parameter_layout({
            {graphics.get_pipeline_parameter_layout("node"),    RENDER_PIPELINE_PARAMETER_TYPE_NODE},
            {graphics.get_pipeline_parameter_layout("texture"),
             RENDER_PIPELINE_PARAMETER_TYPE_MATERIAL                                               }
        });
        m_pipeline = &pipeline;

        main.add_dependency(
            RHI_RENDER_SUBPASS_EXTERNAL,
            RHI_PIPELINE_STAGE_FLAG_COLOR_OUTPUT | RHI_PIPELINE_STAGE_FLAG_EARLY_DEPTH_STENCIL,
            0,
            color_pass.get_index(),
            RHI_PIPELINE_STAGE_FLAG_COLOR_OUTPUT | RHI_PIPELINE_STAGE_FLAG_EARLY_DEPTH_STENCIL,
            RHI_ACCESS_FLAG_COLOR_WRITE | RHI_ACCESS_FLAG_DEPTH_STENCIL_WRITE);

        m_render_graph->compile();

        m_texture = graphics.get_rhi()->create_texture("hello-world/test.jpg");

        rhi_sampler_desc sampler_desc = {};
        sampler_desc.min_filter = RHI_FILTER_LINEAR;
        sampler_desc.mag_filter = RHI_FILTER_LINEAR;
        sampler_desc.address_mode_u = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.address_mode_v = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.address_mode_w = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
        m_sampler = graphics.get_rhi()->create_sampler(sampler_desc);

        m_geometry = std::make_unique<geometry>(graphics.get_rhi());
        std::vector<float3> position = {
            {-0.5f, -0.5f, -0.2f},
            {0.5f,  -0.5f, -0.2f},
            {0.5f,  0.5f,  -0.2f},
            {-0.5f, 0.5f,  -0.2f},
            {-0.5f, -0.5f, 0.2f },
            {0.5f,  -0.5f, 0.2f },
            {0.5f,  0.5f,  0.2f },
            {-0.5f, 0.5f,  0.2f }
        };
        m_geometry->add_attribute("position", position);
        std::vector<float3> color = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 1.0f},
            {0.0f, 0.0f, 1.0f}
        };
        m_geometry->add_attribute("color", color);
        std::vector<float2> uv = {
            {1.0f, 0.0f},
            {0.0f, 0.0f},
            {0.0f, 1.0f},
            {1.0f, 1.0f},
            {1.0f, 0.0f},
            {0.0f, 0.0f},
            {0.0f, 1.0f},
            {1.0f, 1.0f}
        };
        m_geometry->add_attribute("uv", uv);
        std::vector<std::uint32_t> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};
        m_geometry->set_indices(indices);

        material_layout& material_layout = m_render_graph->add_material_layout("text material");
        material_layout.add_pipeline(pipeline);
        material_layout.add_field(
            "texture",
            {.pipeline_index = 0, .field_index = 0, .size = 1, .offset = 0});

        m_material = material_layout.add_material("test");
        m_material->set("texture", m_texture, m_sampler);
    }

    render_pipeline* m_pipeline;

    void tick(float delta)
    {
        auto& window = engine::get_system<window_system>();
        auto rect = window.get_extent();

        if (rect.width == 0 || rect.height == 0)
            return;

        float4x4_simd p = matrix_simd::perspective(
            to_radians(45.0f),
            static_cast<float>(rect.width) / static_cast<float>(rect.height),
            0.1f,
            100.0f);

        float4x4_simd m = matrix_simd::affine_transform(
            simd::set(10.0, 10.0, 10.0, 0.0),
            quaternion_simd::rotation_axis(simd::set(1.0f, 0.0f, 0.0f, 0.0f), m_rotate),
            simd::set(0.0, 0.0, 0.0, 0.0));

        float4x4_simd v = matrix_simd::affine_transform(
            simd::set(1.0f, 1.0f, 1.0f, 0.0f),
            simd::set(0.0f, 0.0f, 0.0f, 1.0f),
            simd::set(0.0, 0.0, -30.0f, 0.0f));
        v = matrix_simd::inverse_transform(v);

        float4x4_simd mvp = matrix_simd::mul(matrix_simd::mul(m, v), p);

        auto transform_ptr = m_test_object->get_component<transform>();
        transform_ptr->set_rotation(
            quaternion_simd::rotation_axis(simd::set(1.0f, 0.0f, 0.0f, 0.0f), m_rotate));

        m_rotate += delta * 2.0f;
        return;

        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time =
            std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime)
                .count();

        m_rotate += delta * 2.0f;
    }

    void resize(std::uint32_t width, std::uint32_t height)
    {
        rhi_context* rhi = engine::get_system<graphics_system>().get_rhi();
        rhi->destroy_depth_stencil_buffer(m_depth_stencil);

        rhi_depth_stencil_buffer_desc depth_stencil_buffer_desc = {};
        depth_stencil_buffer_desc.width = width;
        depth_stencil_buffer_desc.height = height;
        depth_stencil_buffer_desc.samples = RHI_SAMPLE_COUNT_1;
        depth_stencil_buffer_desc.format = RHI_RESOURCE_FORMAT_D24_UNORM_S8_UINT;
        m_depth_stencil = rhi->create_depth_stencil_buffer(depth_stencil_buffer_desc);

        m_render_graph->get_resource("depth stencil buffer").set_resource(m_depth_stencil);
    }

    std::unique_ptr<node> m_test_object;
    std::unique_ptr<geometry> m_geometry;
    material* m_material;

    std::unique_ptr<render_graph> m_render_graph;

    rhi_resource* m_texture;
    rhi_sampler* m_sampler;

    rhi_resource* m_depth_stencil;

    float m_rotate = 0.0f;
};
} // namespace violet::sample

int main()
{
    using namespace violet;

    engine::initialize("hello-world/config");
    engine::install<window_system>();
    engine::install<graphics_system>();
    engine::install<sample::hello_world>();

    engine::get_system<window_system>().on_destroy().then(
        []()
        {
            log::info("Close window");
            engine::exit();
        });

    engine::run();

    return 0;
}