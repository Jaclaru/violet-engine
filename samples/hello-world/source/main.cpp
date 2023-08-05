#include "core/engine.hpp"
#include "core/node/node.hpp"
// #include "graphics/graphics_module.hpp"
// #include "window/window_module.hpp"
#include <filesystem>
#include <fstream>
#include <thread>

namespace violet::sample
{
class hello_world : public engine_system
{
public:
    hello_world() : engine_system("hello_world") {}

    virtual bool initialize(const dictionary& config)
    {
        log::info(config["text"]);

        /*auto& window = engine::get_system<window_system>();
        window.get_task_graph().window_destroy.add_task("exit", []() {
            log::info("close window");
            engine::exit();
        });*/

        m_test_object = std::make_unique<node>("test");

        engine::on_frame_begin().then([]() { log::debug("frame begin"); });
        engine::on_frame_end().then([]() { log::debug("frame end"); });
        engine::on_tick().then([](float delta) { log::debug("tick: {}", delta); });

        return true;
    }

    virtual void shutdown() {}

private:
    std::unique_ptr<node> m_test_object;
};
} // namespace violet::sample

int main()
{
    using namespace violet;

    engine::initialize("");
    // engine::install<window_module>();
    // engine::install<graphics_module>();
    engine::install<sample::hello_world>();
    engine::run();

    return 0;
}