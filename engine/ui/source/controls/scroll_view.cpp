#include "ui/controls/scroll_view.hpp"

namespace ash::ui
{
scroll_bar::scroll_bar(bool vertical, std::uint32_t slider_color, std::uint32_t bar_color)
    : panel(bar_color),
      m_vertical(vertical),
      m_position(0.0f),
      m_slider(std::make_unique<panel>(slider_color))
{
    if (vertical)
    {
        flex_direction(LAYOUT_FLEX_DIRECTION_COLUMN);
        m_slider->on_mouse_drag = [this](int x, int y) {
            auto& bar_extent = extent();
            auto& slider_extent = m_slider->extent();
            float old_position = m_position;
            m_position =
                std::max(static_cast<float>(y) - slider_extent.height * 0.5f, bar_extent.y);
            m_position =
                std::min(bar_extent.y + bar_extent.height - slider_extent.height, m_position);
            if (m_position != old_position)
            {
                m_slider->position(m_position - bar_extent.y, LAYOUT_EDGE_TOP);
                if (on_slide)
                    on_slide(value());
            }
        };
    }
    else
    {
        flex_direction(LAYOUT_FLEX_DIRECTION_ROW);
        m_slider->on_mouse_drag = [this](int x, int y) {
            auto& bar_extent = extent();
            auto& slider_extent = m_slider->extent();
            float old_position = m_position;
            m_position = std::max(static_cast<float>(x) - slider_extent.width * 0.5f, bar_extent.x);
            m_position =
                std::min(bar_extent.x + bar_extent.width - slider_extent.width, m_position);
            if (m_position != old_position)
            {
                m_slider->position(m_position - bar_extent.x, LAYOUT_EDGE_LEFT);
                if (on_slide)
                    on_slide(value());
            }
        };
    }

    m_slider->link(this);
}

void scroll_bar::value(float v) noexcept
{
    float old_value = value();
    if (v == old_value)
        return;

    auto& bar_extent = extent();
    auto& slider_extent = m_slider->extent();

    if (m_vertical)
    {
        m_position = v * (bar_extent.height - slider_extent.height) + bar_extent.y;
        m_slider->position(m_position - bar_extent.y, LAYOUT_EDGE_TOP);
    }
    else
    {
        m_position = v * (bar_extent.width - slider_extent.width) + bar_extent.x;
        m_slider->position(m_position - bar_extent.x, LAYOUT_EDGE_LEFT);
    }
}

float scroll_bar::value() const noexcept
{
    auto& bar_extent = extent();
    auto& slider_extent = m_slider->extent();
    if (m_vertical)
        return (m_position - bar_extent.y) / (bar_extent.height - slider_extent.height);
    else
        return (m_position - bar_extent.x) / (bar_extent.width - slider_extent.width);
}

void scroll_bar::slider_size(float size)
{
    if (m_vertical)
        m_slider->height(size);
    else
        m_slider->width(size);
}

scroll_view::scroll_view(const scroll_view_style& style) : view_panel(style.background_color)
{
    justify_content(LAYOUT_JUSTIFY_CENTER);
    align_items(LAYOUT_ALIGN_CENTER);

    m_vertical_bar = std::make_unique<scroll_bar>(true, style.slider_color, style.bar_color);
    m_vertical_bar->width(style.bar_width);
    m_vertical_bar->height_percent(90.0f);
    m_vertical_bar->position_type(LAYOUT_POSITION_TYPE_ABSOLUTE);
    m_vertical_bar->position(5.0f, LAYOUT_EDGE_RIGHT);
    m_vertical_bar->layer(90);
    m_vertical_bar->on_slide = [this](float value) {
        if (value == 1.0f)
            return;
        update_container_vertical_position(value);
    };
    m_vertical_bar->link(this);

    m_horizontal_bar = std::make_unique<scroll_bar>(false, style.slider_color, style.bar_color);
    m_horizontal_bar->width_percent(90.0f);
    m_horizontal_bar->height(style.bar_width);
    m_horizontal_bar->position_type(LAYOUT_POSITION_TYPE_ABSOLUTE);
    m_horizontal_bar->position(5.0f, LAYOUT_EDGE_BOTTOM);
    m_horizontal_bar->layer(90);
    m_horizontal_bar->on_slide = [this](float value) {
        if (value == 1.0f)
            return;
        update_container_horizontal_position(value);
    };
    m_horizontal_bar->link(this);

    m_container = std::make_unique<element>();
    m_container->position_type(LAYOUT_POSITION_TYPE_ABSOLUTE);
    m_container->on_resize = [this](int width, int height) {
        auto& e = extent();
        update_scroll_bar(e.width, e.height, width, height);
    };
    m_container->link(this);

    on_mouse_wheel = [scroll_speed = style.scroll_speed, this](int whell) -> bool {
        if (m_vertical_bar->display())
        {
            float new_value =
                m_vertical_bar->value() - scroll_speed / m_container->extent().height * whell;
            new_value = math::clamp(new_value, 0.0f, 1.0f);
            m_vertical_bar->value(new_value);
            update_container_vertical_position(new_value);
        }
        return false;
    };
}

void scroll_view::add(element* element)
{
    element->link(m_container.get());
}

void scroll_view::remove(element* element)
{
    element->unlink();
}

void scroll_view::on_extent_change(const element_extent& extent)
{
    view_panel::on_extent_change(extent);

    auto& container_extent = m_container->extent();
    update_scroll_bar(extent.width, extent.height, container_extent.width, container_extent.height);
}

void scroll_view::update_container_vertical_position(float scroll_value)
{
    auto& container_extent = m_container->extent();
    auto& view_extent = extent();
    m_container->position(
        scroll_value * (view_extent.height - container_extent.height),
        LAYOUT_EDGE_TOP);
}

void scroll_view::update_container_horizontal_position(float scroll_value)
{
    auto& container_extent = m_container->extent();
    auto& view_extent = extent();
    m_container->position(
        scroll_value * (view_extent.width - container_extent.width),
        LAYOUT_EDGE_LEFT);
}

void scroll_view::update_scroll_bar(
    float view_width,
    float view_height,
    float container_width,
    float container_height)
{
    if (container_height <= view_height)
    {
        m_vertical_bar->hide();
        m_container->position(0.0f, LAYOUT_EDGE_TOP);
    }
    else
    {
        m_vertical_bar->show();
        m_vertical_bar->slider_size(view_height / container_height * view_height * 0.9f);
    }

    if (container_width <= view_width)
    {
        m_horizontal_bar->hide();
        m_container->position(0.0f, LAYOUT_EDGE_LEFT);
    }
    else
    {
        m_horizontal_bar->show();
        m_horizontal_bar->slider_size(view_width / container_width * view_width * 0.9f);
    }
}
} // namespace ash::ui