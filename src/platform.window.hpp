#pragma once

#include <cassert>
#include <utility>

#include <glm/vec2.hpp>
#include <tl/optional.hpp>

#include "delegate.hpp"

struct SDL_Window;
union SDL_Event;
struct DrawParams;

class PlatformWindow {
private:
    struct PrivateAccessToken {};

public:
    PlatformWindow(PrivateAccessToken, SDL_Window* window) noexcept;

    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    PlatformWindow(PlatformWindow&& rhs) noexcept
        : _window{std::exchange(rhs._window, nullptr)}, Events{std::move(rhs.Events)}, RenderData{rhs.RenderData} {}

    static tl::optional<PlatformWindow> create(tl::optional<glm::ivec2> wnd_size = tl::nullopt);

    struct PollInputStartEvent {};
    struct PollInputEndEvent {};
    using poll_start_event_delegate = cpp::bitwizeshift::delegate<void(const PollInputStartEvent&)>;
    using poll_end_event_delegate = cpp::bitwizeshift::delegate<void(const PollInputEndEvent)>;
    using input_event_delegate = cpp::bitwizeshift::delegate<void(const SDL_Event&)>;
    using render_event_delegate = cpp::bitwizeshift::delegate<void(const DrawParams&)>;

    struct {
        poll_start_event_delegate poll_input_start;
        poll_end_event_delegate poll_input_end;
        input_event_delegate input_event;
        render_event_delegate render_event;
    } Events;

    struct {
        glm::ivec2 surface_size;
        glm::ivec2 window_size;
    } RenderData;

    void event_loop();
    void set_quit() { _quitflag = true; }
    SDL_Window* handle() const noexcept { return _window; }

private:
    SDL_Window* _window;
    bool _quitflag{false};
};
