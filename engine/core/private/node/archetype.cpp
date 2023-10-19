#include "core/node/archetype.hpp"
#include "node/archetype_chunk.hpp"

namespace violet
{
archetype::archetype(
    const std::vector<component_id>& components,
    const component_registry& m_component_infos,
    archetype_chunk_allocator* allocator) noexcept
    : m_component_infos(&m_component_infos),
      m_components(components),
      m_chunk_allocator(allocator),
      m_size(0)
{
    for (component_id id : components)
        m_mask.set(id);
    initialize_layout(components);
}

archetype::~archetype()
{
    clear();
}

std::size_t archetype::add()
{
    std::size_t index = allocate();
    construct(index);

    return index;
}

std::size_t archetype::move(std::size_t index, archetype& target)
{
    assert(this != &target);

    auto [source_chunk_index, source_entity_index] =
        std::div(static_cast<const long>(index), static_cast<const long>(m_entity_per_chunk));

    std::size_t target_index = target.allocate();
    auto [target_chunk_index, target_entity_index] = std::div(
        static_cast<const long>(target_index),
        static_cast<const long>(target.m_entity_per_chunk));

    for (component_id id : m_components)
    {
        if (target.m_mask.test(id))
        {
            auto& info = *m_component_infos->at(id);

            std::size_t source_offset = m_offset[id] + source_entity_index * info.size();
            std::size_t target_offset = target.m_offset[id] + target_entity_index * info.size();
            info.move_construct(
                get_data_pointer(source_chunk_index, source_offset),
                target.get_data_pointer(target_chunk_index, target_offset));
        }
    }

    for (component_id id : target.m_components)
    {
        if (!m_mask.test(id))
        {
            auto& info = *m_component_infos->at(id);

            std::size_t offset = target.m_offset[id] + target_entity_index * info.size();
            info.construct(target.get_data_pointer(target_chunk_index, offset));
        }
    }

    remove(index);
    return target_index;
}

void archetype::remove(std::size_t index)
{
    assert(index < m_size);

    std::size_t back_index = m_size - 1;

    if (index != back_index)
        swap(index, back_index);

    destruct(back_index);
    --m_size;

    if (m_size % m_entity_per_chunk == 0)
    {
        m_chunk_allocator->free(m_chunks.back());
        m_chunks.pop_back();
    }
}

void archetype::clear() noexcept
{
    for (std::size_t i = 0; i < m_size; ++i)
        destruct(i);

    for (archetype_chunk* chunk : m_chunks)
        m_chunk_allocator->free(chunk);
    m_chunks.clear();

    m_size = 0;
}

void archetype::initialize_layout(const std::vector<component_id>& components)
{
    m_entity_per_chunk = 0;

    struct layout_info
    {
        component_id id;
        std::size_t size;
        std::size_t align;
    };

    std::vector<layout_info> list(components.size());
    std::transform(components.cbegin(), components.cend(), list.begin(), [this](component_id id) {
        return layout_info{
            id,
            m_component_infos->at(id)->size(),
            m_component_infos->at(id)->align()};
    });

    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.align == b.align ? a.id < b.id : a.align > b.align;
    });

    std::size_t entity_size = 0;
    for (const auto& info : list)
        entity_size += info.size;

    m_entity_per_chunk = archetype_chunk::CHUNK_SIZE / entity_size;

    std::size_t offset = 0;
    for (const auto& info : list)
    {
        m_offset[info.id] = static_cast<std::uint16_t>(offset);
        offset += info.size * m_entity_per_chunk;
    }
}

std::size_t archetype::allocate()
{
    std::size_t index = m_size;
    if (index >= capacity())
        m_chunks.push_back(m_chunk_allocator->allocate());

    ++m_size;
    return index;
}

void archetype::construct(std::size_t index)
{
    auto [chunk_index, entity_index] =
        std::div(static_cast<const long>(index), static_cast<const long>(m_entity_per_chunk));

    for (component_id id : m_components)
    {
        auto& info = *m_component_infos->at(id);

        std::size_t offset = m_offset[id] + entity_index * info.size();
        info.construct(get_data_pointer(chunk_index, offset));
    }
}

void archetype::destruct(std::size_t index)
{
    auto [chunk_index, entity_index] =
        std::div(static_cast<const long>(index), static_cast<const long>(m_entity_per_chunk));

    for (component_id id : m_components)
    {
        auto& info = *m_component_infos->at(id);

        std::size_t offset = m_offset[id] + entity_index * info.size();
        info.destruct(get_data_pointer(chunk_index, offset));
    }
}

void archetype::swap(std::size_t a, std::size_t b)
{
    auto [a_chunk_index, a_entity_index] =
        std::div(static_cast<const long>(a), static_cast<const long>(m_entity_per_chunk));

    auto [b_chunk_index, b_entity_index] =
        std::div(static_cast<const long>(b), static_cast<const long>(m_entity_per_chunk));

    for (component_id id : m_components)
    {
        auto& info = *m_component_infos->at(id);

        std::size_t a_offset = m_offset[id] + a_entity_index * info.size();
        std::size_t b_offset = m_offset[id] + b_entity_index * info.size();
        info.swap(
            get_data_pointer(a_chunk_index, a_offset),
            get_data_pointer(b_chunk_index, b_offset));
    }
}

void* archetype::get_data_pointer(std::size_t chunk_index, std::size_t offset)
{
    return &m_chunks[chunk_index]->data[offset];
}
} // namespace violet