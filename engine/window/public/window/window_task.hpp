#pragma once

#include "core/task/task.hpp"
#include "window/input.hpp"

namespace violet
{
constexpr const char* TASK_NAME_WINDOW_TICK = "window tick";

class window_task
{
public:
    window_task();
    /*
    task_graph<mouse_mode, int, int> mouse_move;
    task_graph<mouse_key, key_state> mouse_key;
    task_graph<keyboard_key, key_state> keyboard_key;
    task_graph<char> keyboard_char;
    task_graph<std::uint32_t, std::uint32_t> window_resize;
    task_graph<> window_destroy;
    */
};
} // namespace violet