#include "components/camera.hpp"
#include "components/mesh.hpp"
#include "components/orbit_control.hpp"
#include "components/transform.hpp"
#include "control/control_system.hpp"
#include "core/engine.hpp"
#include "core/node/node.hpp"
#include "graphics/graphics_system.hpp"
#include "physics/physics_system.hpp"
#include "physics/physics_world.hpp"
#include "window/window_system.hpp"

namespace violet::sample
{
class color_pipeline : public render_pipeline
{
public:
    color_pipeline(render_context* context) : render_pipeline(context)
    {
        set_shader("physics/shaders/basic.vert.spv", "physics/shaders/basic.frag.spv");
        set_vertex_attributes({
            {"position", RHI_RESOURCE_FORMAT_R32G32B32_FLOAT},
            {"color",    RHI_RESOURCE_FORMAT_R32G32B32_FLOAT}
        });
        set_cull_mode(RHI_CULL_MODE_NONE);

        rhi_parameter_layout* material_layout = context->add_parameter_layout(
            "color pipeline",
            {
                {RHI_PARAMETER_TYPE_SHADER_RESOURCE, 1}
        });

        set_parameter_layouts({
            {context->get_parameter_layout("violet mesh"),   RENDER_PIPELINE_PARAMETER_TYPE_MESH    },
            {material_layout,                                RENDER_PIPELINE_PARAMETER_TYPE_MATERIAL},
            {context->get_parameter_layout("violet camera"),
             RENDER_PIPELINE_PARAMETER_TYPE_CAMERA                                                  }
        });
    }

private:
    void render(rhi_render_command* command, render_data& data)
    {
        for (render_mesh& mesh : data.meshes)
        {
            command->set_vertex_buffers(mesh.vertex_buffers.data(), mesh.vertex_buffers.size());
            command->set_index_buffer(mesh.index_buffer);
            command->set_parameter(0, mesh.node);
            command->set_parameter(1, mesh.material);
            command->set_parameter(2, data.camera_parameter);
            command->draw_indexed(0, 36, 0);
        }
    }
};

class physics_demo : public engine_system
{
public:
    physics_demo() : engine_system("physics_demo"), m_depth_stencil(nullptr) {}

    virtual bool initialize(const dictionary& config)
    {
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
        intiialize_physics();

        {
            m_object = std::make_unique<node>("test", engine::get_world());
            auto [mesh_ptr, transform_ptr, rigidbody_ptr] =
                m_object->add_component<mesh, transform, rigidbody>();
            mesh_ptr->set_geometry(m_geometry);
            mesh_ptr->add_submesh(0, 0, 12, m_material);

            rigidbody_ptr->set_transform(transform_ptr->get_world_matrix());
            rigidbody_ptr->set_type(PEI_RIGIDBODY_TYPE_DYNAMIC);
            rigidbody_ptr->set_shape(m_collision_shape);
            rigidbody_ptr->set_mass(1.0f);
            m_physics_world->add(rigidbody_ptr);
        }

        {
            m_plane = std::make_unique<node>("plane", engine::get_world());
            auto [mesh_ptr, transform_ptr, rigidbody_ptr] =
                m_plane->add_component<mesh, transform, rigidbody>();
            mesh_ptr->set_geometry(m_geometry);
            mesh_ptr->add_submesh(0, 0, 12, m_material);

            transform_ptr->set_position(0.0f, -3.0f, 0.0f);

            rigidbody_ptr->set_transform(transform_ptr->get_world_matrix());
            rigidbody_ptr->set_type(PEI_RIGIDBODY_TYPE_KINEMATIC);
            rigidbody_ptr->set_shape(m_collision_shape);
            rigidbody_ptr->set_mass(0.0f);
            m_physics_world->add(rigidbody_ptr);
        }

        return true;
    }

    virtual void shutdown()
    {
        m_object = nullptr;
        m_plane = nullptr;
        m_camera = nullptr;

        m_render_graph = nullptr;
        m_geometry = nullptr;

        rhi_renderer* rhi = engine::get_system<graphics_system>().get_rhi();
        rhi->destroy_depth_stencil_buffer(m_depth_stencil);

        pei_plugin* pei = engine::get_system<physics_system>().get_pei();
        pei->destroy_collision_shape(m_collision_shape);
    }

private:
    void initialize_render()
    {
        auto& graphics = engine::get_system<graphics_system>();
        auto& window = engine::get_system<window_system>();
        auto extent = window.get_extent();

        m_render_graph = std::make_unique<render_graph>(graphics.get_rhi());

        render_pass* main = m_render_graph->add_render_pass("main");

        render_attachment* output_attachment = main->add_attachment("output");
        output_attachment->set_format(graphics.get_rhi()->get_back_buffer()->get_format());
        output_attachment->set_initial_state(RHI_RESOURCE_STATE_UNDEFINED);
        output_attachment->set_final_state(RHI_RESOURCE_STATE_PRESENT);
        output_attachment->set_load_op(RHI_ATTACHMENT_LOAD_OP_CLEAR);
        output_attachment->set_store_op(RHI_ATTACHMENT_STORE_OP_STORE);

        render_attachment* depth_stencil_attachment = main->add_attachment("depth stencil");
        depth_stencil_attachment->set_format(RHI_RESOURCE_FORMAT_D24_UNORM_S8_UINT);
        depth_stencil_attachment->set_initial_state(RHI_RESOURCE_STATE_UNDEFINED);
        depth_stencil_attachment->set_final_state(RHI_RESOURCE_STATE_DEPTH_STENCIL);
        depth_stencil_attachment->set_load_op(RHI_ATTACHMENT_LOAD_OP_CLEAR);
        depth_stencil_attachment->set_store_op(RHI_ATTACHMENT_STORE_OP_DONT_CARE);
        depth_stencil_attachment->set_stencil_load_op(RHI_ATTACHMENT_LOAD_OP_CLEAR);
        depth_stencil_attachment->set_stencil_store_op(RHI_ATTACHMENT_STORE_OP_DONT_CARE);

        render_subpass* color_pass = main->add_subpass("color");
        color_pass->add_reference(
            output_attachment,
            RHI_ATTACHMENT_REFERENCE_TYPE_COLOR,
            RHI_RESOURCE_STATE_RENDER_TARGET);
        color_pass->add_reference(
            depth_stencil_attachment,
            RHI_ATTACHMENT_REFERENCE_TYPE_DEPTH_STENCIL,
            RHI_RESOURCE_STATE_DEPTH_STENCIL);

        render_pipeline* pipeline = color_pass->add_pipeline<color_pipeline>("color");

        main->add_dependency(
            nullptr,
            RHI_PIPELINE_STAGE_FLAG_COLOR_OUTPUT | RHI_PIPELINE_STAGE_FLAG_EARLY_DEPTH_STENCIL,
            0,
            color_pass,
            RHI_PIPELINE_STAGE_FLAG_COLOR_OUTPUT | RHI_PIPELINE_STAGE_FLAG_EARLY_DEPTH_STENCIL,
            RHI_ACCESS_FLAG_COLOR_WRITE | RHI_ACCESS_FLAG_DEPTH_STENCIL_WRITE);

        m_render_graph->compile();

        m_geometry = m_render_graph->add_geometry("test model");
        std::vector<float3> position = {
            {-0.5f, -0.5f, 0.5f },
            {0.5f,  -0.5f, 0.5f },
            {0.5f,  0.5f,  0.5f },
            {-0.5f, 0.5f,  0.5f },
            {-0.5f, -0.5f, -0.5f},
            {0.5f,  -0.5f, -0.5f},
            {0.5f,  0.5f,  -0.5f},
            {-0.5f, 0.5f,  -0.5 }
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
        std::vector<std::uint32_t> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 7, 3, 0, 0, 4, 7,
                                              1, 5, 6, 6, 2, 1, 3, 2, 6, 6, 7, 3, 0, 1, 5, 5, 4, 0};
        m_geometry->set_indices(indices);

        material_layout* material_layout = m_render_graph->add_material_layout("text material");
        material_layout->add_pipeline(pipeline);
        material_layout->add_field(
            "texture",
            {.pipeline_index = 0, .field_index = 0, .size = 1, .offset = 0});

        m_material = material_layout->add_material("test");

        m_camera = std::make_unique<node>("main camera", engine::get_world());
        auto [camera_ptr, transform_ptr, orbit_control_ptr] =
            m_camera->add_component<camera, transform, orbit_control>();
        camera_ptr->set_render_pass(main);
        camera_ptr->set_attachment(0, graphics.get_rhi()->get_back_buffer(), true);
        camera_ptr->resize(extent.width, extent.height);

        transform_ptr->set_position(0.0f, 0.0f, -10.0f);

        orbit_control_ptr->r = 10.0f;

        resize(extent.width, extent.height);
    }

    void intiialize_physics()
    {
        auto& physics = engine::get_system<physics_system>();

        m_physics_world =
            std::make_unique<physics_world>(float3{0.0f, -9.8f, 0.0f}, physics.get_pei());

        pei_collision_shape_desc shape_desc = {};
        shape_desc.type = PEI_COLLISION_SHAPE_TYPE_BOX;
        shape_desc.box.height = 1.0f;
        shape_desc.box.width = 1.0f;
        shape_desc.box.length = 1.0f;
        m_collision_shape = physics.get_pei()->create_collision_shape(shape_desc);
    }

    void tick(float delta)
    {
        auto& physics = engine::get_system<physics_system>();
        physics.simulation(m_physics_world.get());

        return;

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

        auto transform_ptr = m_object->get_component<transform>();
        transform_ptr->set_rotation(
            quaternion_simd::rotation_axis(simd::set(1.0f, 0.0f, 0.0f, 0.0f), m_rotate));

        m_rotate += delta * 2.0f;
    }

    void resize(std::uint32_t width, std::uint32_t height)
    {
        rhi_renderer* rhi = engine::get_system<graphics_system>().get_rhi();
        rhi->destroy_depth_stencil_buffer(m_depth_stencil);

        rhi_depth_stencil_buffer_desc depth_stencil_buffer_desc = {};
        depth_stencil_buffer_desc.width = width;
        depth_stencil_buffer_desc.height = height;
        depth_stencil_buffer_desc.samples = RHI_SAMPLE_COUNT_1;
        depth_stencil_buffer_desc.format = RHI_RESOURCE_FORMAT_D24_UNORM_S8_UINT;
        m_depth_stencil = rhi->create_depth_stencil_buffer(depth_stencil_buffer_desc);

        // m_render_graph->get_resource("depth stencil buffer")->set_resource(m_depth_stencil);

        if (m_camera)
        {
            auto camera_ptr = m_camera->get_component<camera>();
            camera_ptr->resize(width, height);
            camera_ptr->set_attachment(1, m_depth_stencil);
        }
    }

    std::unique_ptr<node> m_camera;
    std::unique_ptr<node> m_object;
    std::unique_ptr<node> m_plane;

    geometry* m_geometry;
    material* m_material;

    std::unique_ptr<render_graph> m_render_graph;
    rhi_resource* m_depth_stencil;

    std::unique_ptr<physics_world> m_physics_world;
    pei_collision_shape* m_collision_shape;

    float m_rotate = 0.0f;
};
} // namespace violet::sample

int main()
{
    using namespace violet;

    engine::initialize("physics/config");
    engine::install<window_system>();
    engine::install<graphics_system>();
    engine::install<physics_system>();
    engine::install<control_system>();
    engine::install<sample::physics_demo>();

    engine::get_system<window_system>().on_destroy().then(
        []()
        {
            log::info("Close window");
            engine::exit();
        });

    engine::run();

    return 0;
}