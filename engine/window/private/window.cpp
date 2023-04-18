#include "window/window.hpp"
#include "core/context/engine.hpp"
#include "window/window_task.hpp"
#include "window_impl.hpp"
#include "window_impl_win32.hpp"

namespace violet
{
window::window()
    : engine_module("window"),
      m_impl(std::make_unique<window_impl_win32>()),
      m_mouse(m_impl.get())
{
}

window::~window()
{
}

bool window::initialize(const dictionary& config)
{
    if (!m_impl->initialize(config["width"], config["height"], config["title"]))
        return false;

    m_title = config["title"];

    engine::get_task_graph().tick.add_task(
        "window tick",
        [this](float delta) { tick(); },
        TASK_OPTION_MAIN_THREAD);

    return true;
}

void window::shutdown()
{
    m_impl->shutdown();
}

void window::tick()
{
    auto& task_executor = engine::get_task_executor();

    m_mouse.tick();
    m_keyboard.tick();

    m_impl->reset();
    m_impl->tick();

    for (auto& message : m_impl->get_messages())
    {
        switch (message.type)
        {
        case window_message::message_type::MOUSE_MOVE: {
            m_mouse.m_x = message.mouse_move.x;
            m_mouse.m_y = message.mouse_move.y;

            task_executor.execute_sync(
                m_task_graph.mouse_move,
                m_mouse.get_mode(),
                message.mouse_move.x,
                message.mouse_move.y);
            break;
        }
        case window_message::message_type::MOUSE_KEY: {
            if (message.mouse_key.down)
                m_mouse.key_down(message.mouse_key.key);
            else
                m_mouse.key_up(message.mouse_key.key);

            task_executor.execute_sync(
                m_task_graph.mouse_key,
                message.mouse_key.key,
                m_mouse.key(message.mouse_key.key));
            break;
        }
        case window_message::message_type::MOUSE_WHELL: {
            m_mouse.m_whell = message.mouse_whell;
            break;
        }
        case window_message::message_type::KEYBOARD_KEY: {
            if (message.keyboard_key.down)
                m_keyboard.key_down(message.keyboard_key.key);
            else
                m_keyboard.key_up(message.keyboard_key.key);

            task_executor.execute_sync(
                m_task_graph.keyboard_key,
                message.keyboard_key.key,
                m_keyboard.key(message.keyboard_key.key));
            break;
        }
        case window_message::message_type::KEYBOARD_CHAR: {
            task_executor.execute_sync(m_task_graph.keyboard_char, message.keyboard_char);
            break;
        }
        case window_message::message_type::WINDOW_MOVE: {
            break;
        }
        case window_message::message_type::WINDOW_RESIZE: {
            task_executor.execute_sync(
                m_task_graph.window_resize,
                message.window_resize.width,
                message.window_resize.height);
            break;
        }
        case window_message::message_type::WINDOW_DESTROY: {
            task_executor.execute_sync(m_task_graph.window_destroy);
            break;
        }
        default:
            break;
        }
    }
}

void* window::get_handle() const
{
    return m_impl->get_handle();
}

rect<std::uint32_t> window::get_extent() const
{
    return m_impl->get_extent();
}

void window::set_title(std::string_view title)
{
    m_title = title;
    m_impl->set_title(title);
}
} // namespace violet