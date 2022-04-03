#pragma once

#include "archetype.hpp"
#include "entity.hpp"
#include "view.hpp"
#include <atomic>
#include <queue>

namespace ash::ecs
{
class mask_archetype;
struct entity_record
{
    mask_archetype* archetype;
    std::size_t index;
};

class mask_archetype : public archetype
{
public:
    mask_archetype(
        const archetype_layout& layout,
        const std::unordered_map<component_id, component_index>& index_map)
        : archetype(layout)
    {
        for (const auto& [id, info] : layout)
            m_mask.set(index_map.at(id), true);
    }

    void add(entity_record* record)
    {
        archetype::add();

        record->archetype = this;
        record->index = m_record.size();

        m_record.push_back(record);
    }

    void remove(std::size_t index)
    {
        archetype::remove(index);

        if (m_record[index] != m_record.back())
        {
            m_record[index] = m_record.back();
            m_record[index]->index = index;
        }
        m_record.pop_back();
    }

    void move(std::size_t index, mask_archetype& target)
    {
        archetype::move(index, target);

        entity_record* target_record = m_record[index];
        target_record->archetype = &target;
        target_record->index = target.m_record.size();
        target.m_record.push_back(target_record);

        if (target_record != m_record.back())
        {
            m_record[index] = m_record.back();
            m_record[index]->index = index;
        }
        m_record.pop_back();
    }

    const component_mask& mask() const noexcept { return m_mask; }

private:
    component_mask m_mask;
    std::vector<entity_record*> m_record;
};

class world
{
private:
    template <typename T>
    struct index_generator
    {
    public:
        using index_type = T;

    public:
        index_generator(index_type base = 0) : m_next(base) {}
        index_type new_index() { return m_next.fetch_add(1); }

    private:
        std::atomic<index_type> m_next;
    };

public:
    world() { register_component<entity>(); }
    ~world()
    {
        for (auto& [_, archetype] : m_archetypes)
        {
            archetype->clear();
        }
    }

    template <typename... Components>
    void register_component()
    {
        component_list<Components...>::each([this]<typename T>() {
            auto iter = m_component_index.find(component_id_v<T>);
            if (iter != m_component_index.end())
                return;

            component_index index = m_component_index_generator.new_index();
            m_component_index[component_id_v<T>] = index;

            auto info = std::make_unique<component_info>();
            info->size = sizeof(T);
            info->align = alignof(T);
            info->construct = [](void* target) { new (target) T(); };
            info->move_construct = [](void* source, void* target) {
                new (target) T(std::move(*static_cast<T*>(source)));
            };
            info->destruct = [](void* target) { static_cast<T*>(target)->~T(); };
            info->swap = [](void* a, void* b) {
                std::swap(*static_cast<T*>(a), *static_cast<T*>(b));
            };
            if (m_component_info.size() <= index)
                m_component_info.resize(index + 1);
            m_component_info[index] = std::move(info);
        });
    }

    entity_id create() { return m_entity_index_generator.new_index(); }

    void release(entity_id id) {}

    template <typename... Components>
    void add(entity_id id)
    {
        auto& record = m_entity_record[id];
        if (record.archetype == nullptr)
        {
            mask_archetype* archetype = get_or_create_archetype<entity, Components...>();
            archetype->add(&record);
        }
        else
        {
            auto iter = m_archetypes.find(record.archetype->mask() | make_mask<Components...>());
            if (iter == m_archetypes.cend())
            {
                archetype_layout layout = record.archetype->layout();
                layout.insert(make_component_set<Components...>());

                mask_archetype* target = make_archetype(layout);
                record.archetype->move(record.index, *target);
            }
            else
            {
                record.archetype->move(record.index, *iter->second);
            }
        }
    }

    template <typename... Components>
    void remove(entity_id id)
    {
        auto& record = m_entity_record[id];
        mask_archetype* source = record.archetype;

        component_mask mask = source->mask() ^ make_mask<Components...>();
        auto iter = m_archetypes.find(mask);
        if (iter == m_archetypes.cend())
        {
            archetype_layout layout = source->layout();
            layout.erase(make_component_set<Components...>());

            mask_archetype* target = make_archetype(layout);
            source->move(record.index, *target);
        }
        else
        {
            source->move(record.index, *iter->second);
        }
    }

    template <typename T>
    T& component(entity_id id)
    {
        auto& record = m_entity_record[id];
        auto handle = record.archetype->begin<T>() + record.index;

        return handle.component<T>();
    }

    template <typename... Components>
    view<Components...>* make_view()
    {
        component_mask m = make_mask<Components...>();
        auto v = std::make_unique<view<Components...>>(m);

        for (auto& [mask, archetype] : m_archetypes)
        {
            if ((m & mask) == m)
                v->add_archetype(archetype.get());
        }

        auto result = v.get();
        m_views.push_back(std::move(v));
        return result;
    }

private:
    template <typename... Components>
    component_set make_component_set()
    {
        component_set result;
        component_list<Components...>::each([&result, this]<typename T>() {
            component_index index = m_component_index[component_id_v<T>];
            result.push_back(std::make_pair(component_id_v<T>, m_component_info[index].get()));
        });
        return result;
    }

    template <typename... Components>
    component_mask make_mask()
    {
        component_mask result;
        (result.set(m_component_index[component_id_v<Components>]), ...);
        return result;
    }

    template <typename... Components>
    mask_archetype* get_or_create_archetype()
    {
        component_mask mask = make_mask<Components...>();
        auto& result = m_archetypes[mask];

        if (result == nullptr)
            return make_archetype<Components...>();
        else
            return result.get();
    }

    template <typename... Components>
    mask_archetype* make_archetype()
    {
        archetype_layout layout;
        layout.insert(make_component_set<Components...>());
        return make_archetype(layout);
    }

    mask_archetype* make_archetype(const archetype_layout& layout)
    {
        auto result = std::make_unique<mask_archetype>(layout, m_component_index);

        for (auto& v : m_views)
        {
            if ((v->mask() & result->mask()) == v->mask())
                v->add_archetype(result.get());
        }

        return (m_archetypes[result->mask()] = std::move(result)).get();
    }

    std::unordered_map<entity_id, entity_record> m_entity_record;
    std::unordered_map<component_mask, std::unique_ptr<mask_archetype>> m_archetypes;

    std::unordered_map<component_id, component_index> m_component_index;
    std::vector<std::unique_ptr<component_info>> m_component_info;

    std::vector<std::unique_ptr<view_base>> m_views;

    index_generator<entity_id> m_entity_index_generator;
    index_generator<component_index> m_component_index_generator;
};
} // namespace ash::ecs