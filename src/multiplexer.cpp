#include "multiplexer.hpp"
#include "executor.hpp"
#include "config.hpp"

#include <iostream>

// ─────────────────────────────────────────────
//  multiplexer.cpp
// ─────────────────────────────────────────────

namespace pf {

void EventMultiplexer::run()
{
    std::cout << "[mux] Started. Waiting for events from "
              << g_config.devices.size() << " device(s).\n";

    while (g_running) {
        // ── In-process config + mapper reload (SIGHUP) ──
        if (g_reload.exchange(false)) {
            std::cout << "[mux] Reloading config: "
                      << g_config.config_path << "\n";
            if (ConfigLoader::load(g_config.config_path))
                mapper_.init();
            else
                std::cerr << "[mux] Reload failed – keeping current config.\n";
        }

        auto maybe = queue_.pop(); // waits up to 200 ms
        if (!maybe.has_value()) continue;

        const InputEvent& ev = *maybe;

        // Only act on key-press (value == 1).
        // Still notify observers for releases/repeats so event streams
        // show the full picture.
        if (ev.value != EV_KEY_PRESS) {
            notify(ev, {});
            continue;
        }

        if (g_config.debug) {
            std::cout << "[mux] " << ev.device_id
                      << " → " << Mapper::code_to_name(ev.code)
                      << " (" << ev.code << ")\n";
        }

        auto cmd_opt = mapper_.lookup(ev.device_id, ev.code);
        std::string cmd = cmd_opt.value_or("");

        notify(ev, cmd);

        if (!cmd.empty()) {
            std::cout << "[mux] [" << ev.device_id << "] Execute: "
                      << cmd << "\n";
            Executor::run(cmd);
        } else if (g_config.debug) {
            std::cout << "[mux] No binding for "
                      << ev.device_id << "/"
                      << Mapper::code_to_name(ev.code) << "\n";
        }
    }

    std::cout << "[mux] Stopped.\n";
}

int EventMultiplexer::add_observer(EventObserver obs)
{
    std::lock_guard<std::mutex> lock(obs_mu_);
    int id = next_obs_id_++;
    observers_[id] = std::move(obs);
    return id;
}

void EventMultiplexer::remove_observer(int id)
{
    std::lock_guard<std::mutex> lock(obs_mu_);
    observers_.erase(id);
}

void EventMultiplexer::notify(const InputEvent& ev, const std::string& cmd)
{
    // Copy the map while holding the lock, then call without it to
    // prevent deadlock if an observer tries to remove itself.
    std::map<int, EventObserver> local;
    {
        std::lock_guard<std::mutex> lock(obs_mu_);
        local = observers_;
    }
    for (auto& [id, obs] : local)
        obs(ev, cmd);
}

} // namespace pf
