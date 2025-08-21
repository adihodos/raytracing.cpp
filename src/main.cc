#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <mutex>
#include <random>
#include <ranges>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#ifndef _WIN32
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>
#endif

#include <tl/expected.hpp>
#include <tl/optional.hpp>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include <quill/std/Vector.h>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include <SDL3/SDL.h>
#include <glad/glad.h>

#include <zmq.h>
#include <zpp_bits.h>

#include <glm/gtx/vec_swizzle.hpp>
#include <glm/vec2.hpp>

#include <atomic_queue/atomic_queue.h>
#include <lyra/lyra.hpp>

#include "color.hpp"
#include "memory.arena.hpp"
#include "misc.things.hpp"
#include "platform.window.hpp"
#include "random.number.gen.hpp"
#include "ray.hpp"
#include "ray.tracer.core.hpp"
#include "ray.tracer.image.display.hpp"
#include "ray.tracer.object.defs.hpp"
#include "short_alloc.hpp"
#include "ui.backend.nuklear.hpp"

#pragma GCC optimize("O0")

quill::Logger* g_logger{};

enum class RunMode : uint8_t { Client, Server };

struct ProgramOptions {
    uint16_t port{5555};
    std::string msg_header{"__msg__|"};
    int32_t sigfd{-1};
};

template <typename Fn, typename... FnArgs>
    requires std::is_invocable_v<Fn, FnArgs...> && std::is_convertible_v<std::invoke_result_t<Fn, FnArgs...>, int32_t>
auto eintr_wrap_syscall(Fn&& f, FnArgs&&... args) -> std::invoke_result_t<Fn, FnArgs...> {
    int32_t syscall_result{-1};
    do {
        syscall_result = std::invoke(std::forward<Fn>(f), std::forward<FnArgs>(args)...);
    } while (syscall_result == -1 && errno == EINTR);

    return syscall_result;
}

template <typename ZmqFN, typename... FnArgs>
    requires std::is_invocable_v<ZmqFN, FnArgs...>
auto wrap_zmq_call(const char* zmq_fn_name, ZmqFN& zmq_fn, FnArgs&&... args) -> std::invoke_result_t<ZmqFN, FnArgs...> {
    using call_return_type_t = std::invoke_result_t<ZmqFN, FnArgs...>;
    if constexpr (std::is_same_v<void, call_return_type_t>) {
        std::invoke(std::forward<ZmqFN>(zmq_fn), std::forward<FnArgs>(args)...);
    } else {
        auto call_result = std::invoke(std::forward<ZmqFN>(zmq_fn), std::forward<FnArgs>(args)...);
        if constexpr (std::is_convertible_v<decltype(call_result), int32_t>) {
            while (call_result == -1 && zmq_errno() == EINTR) {
                call_result = std::invoke(std::forward<ZmqFN>(zmq_fn), std::forward<FnArgs>(args)...);
            }

            if (call_result == -1 && zmq_errno() != EAGAIN) {
                const int32_t err_zmq = zmq_errno();
                LOG_ERROR(g_logger, "{} : {} ({})", zmq_fn_name, err_zmq, zmq_strerror(err_zmq));
            }
        } else if constexpr (std::is_convertible_v<decltype(call_result), void*>) {
            if (call_result == nullptr) {
                const int32_t err_zmq = zmq_errno();
                LOG_ERROR(g_logger, "{} : {} ({})", zmq_fn_name, err_zmq, zmq_strerror(err_zmq));
            }
        } else {
            static_assert(false, "Unhandled return type");
        }
        return call_result;
    }
}

#define WRAP_ZMQ_FUNC(funcname, ...) wrap_zmq_call(#funcname, funcname, ##__VA_ARGS__)

#ifndef _WIN32
void run_as_server(const ProgramOptions prog_opts) {
    void* z_context = WRAP_ZMQ_FUNC(zmq_ctx_new);
    SCOPED_GUARD([z_context]() { zmq_ctx_destroy(z_context); });
    void* z_srv_socket = WRAP_ZMQ_FUNC(zmq_socket, z_context, ZMQ_PUB);
    SCOPED_GUARD([z_srv_socket]() { zmq_close(z_srv_socket); });

    char scratch_buffer[256]{};
    fmt::format_to(scratch_buffer, "tcp://*:{}", prog_opts.port);
    if (const int32_t res = zmq_bind(z_srv_socket, scratch_buffer); res != 0) {
        LOG_ERROR(g_logger, "Failed to bind socket {} - {}", zmq_errno(), zmq_strerror(zmq_errno()));
        return;
    }

    LOG_INFO(g_logger, "ZContext {}, server socket {} @ {}", fmt::ptr(z_context), fmt::ptr(z_srv_socket),
             scratch_buffer);

    std::random_device rd{};
    std::mt19937 rnd_gen{rd()};
    std::uniform_int_distribution<> d_zipcode{9999, 999999};
    std::uniform_int_distribution<> d_temp{-50, 50};
    std::uniform_int_distribution<> d_humidity{0, 100};

    zmq_pollitem_t poll_items[] = {{nullptr, prog_opts.sigfd, ZMQ_POLLIN, 0}};
    std::function<bool(const zmq_pollitem_t&)> poll_handlers[] = {
        [](const zmq_pollitem_t& polled) {
            bool quit{false};
            if (polled.revents & ZMQ_POLLIN) {
                signalfd_siginfo sig_buffer[4];
                const ssize_t bytes_out = eintr_wrap_syscall(read, polled.fd, sig_buffer, sizeof(sig_buffer));
                if (bytes_out > 0) {
                    for (int32_t idx = 0; idx < static_cast<int32_t>(bytes_out / sizeof(signalfd_siginfo)); ++idx) {
                        const auto& s = sig_buffer[idx];
                        LOG_INFO(g_logger, "Signal caught: {} {} {}", s.ssi_signo, s.ssi_code, s.ssi_pid);

                        if (s.ssi_signo == SIGINT || s.ssi_signo == SIGTERM || s.ssi_signo == SIGQUIT) {
                            LOG_INFO(g_logger, "Server: got quit message, stopping ...");
                            quit = true;
                        }
                    }
                }
            }
            return quit;
        },
    };

    static_assert(std::size(poll_items) == std::size(poll_handlers), "Polling arrays mismatch");

    while (true) {
        const int32_t poll_count =
            WRAP_ZMQ_FUNC(zmq_poll, poll_items, static_cast<int32_t>(std::size(poll_items)), 1000);

        if (poll_count > 0) {
            for (size_t idx = 0, count = std::size(poll_items); idx < count; ++idx) {
                const bool quit_flag = poll_handlers[idx](poll_items[idx]);
                if (quit_flag) {
                    return;
                }
            }
        }

        const int32_t zipcode = d_zipcode(rnd_gen);
        const int32_t temp = d_temp(rnd_gen);
        const int32_t humidity = d_humidity(rnd_gen);

        char str_buffer[512];
        auto [itr, res] = fmt::format_to(str_buffer, "{}{} :: {} :: {}", prog_opts.msg_header, zipcode, temp, humidity);
        *itr = 0;

        const int send_res = WRAP_ZMQ_FUNC(zmq_send, z_srv_socket, str_buffer, strlen(str_buffer), ZMQ_DONTWAIT);
        // std::this_thread::sleep_for(std::chrono::seconds{ 1 });
    }
}

void run_as_client(const ProgramOptions& prog_opts) {
    void* z_ctx = zmq_ctx_new();
    auto guard_zctx = Finally{[z_ctx]() { zmq_ctx_destroy(z_ctx); }};

    void* z_sock = zmq_socket(z_ctx, ZMQ_SUB);
    auto guard_zsock = Finally{[z_sock]() { zmq_close(z_sock); }};

    char scratch_buff[1024]{};
    fmt::format_to(scratch_buff, "tcp://localhost:{}", prog_opts.port);
    if (const auto bind_res = zmq_connect(z_sock, scratch_buff); bind_res != 0) {
        LOG_ERROR(g_logger, "Failed to connext socket to address {}, error {}", scratch_buff,
                  zmq_strerror(zmq_errno()));
        return;
    }

    if (const auto setopt =
            zmq_setsockopt(z_sock, ZMQ_SUBSCRIBE, prog_opts.msg_header.c_str(), prog_opts.msg_header.length());
        setopt != 0) {
        LOG_ERROR(g_logger, "Failed to setsockopt, error {}", zmq_strerror(zmq_errno()));
        return;
    }

    while (true) {
        const int result = zmq_recv(z_sock, scratch_buff, std::size(scratch_buff) - 1, 0);
        if (result < 0) {
            LOG_ERROR(g_logger, "revc fail {}", zmq_strerror(zmq_errno()));
            continue;
        }

        const int size = std::min<size_t>(std::size(scratch_buff) - 1, result);
        scratch_buff[size] = 0;
        LOG_INFO(g_logger, "Client msg: {}", scratch_buff);
    }
}
#endif

struct ThreadMessageA {
    using serialize = zpp::bits::members<3>;
    int32_t x;
    int32_t y;
    char str[32];
};

struct ThreadMessageB {
    using serialize = zpp::bits::members<3>;
    char msg[64];
    uint8_t a;
    uint8_t b;
};

struct WorkerResponse {
    using serialize = zpp::bits::members<3>;
    uint32_t id;
    uint32_t len;
    char payload[64];
};

struct ThreadQuitMessage {
    uint8_t dummy;
};

struct RaytracedPixel {
    uint32_t rtp_x;
    uint32_t rtp_y;
    uint32_t rtp_color;
};

using ThreadPackage = std::variant<RaytracedPixel, ThreadMessageA, ThreadMessageB, WorkerResponse, ThreadQuitMessage>;

constexpr uint32_t kMaxWorkers = 8;

struct WorkerState {
    std::string channel;
    std::thread thread;
    void* z_socket;
};

struct WorkerResponseHandlerContext {
    uint32_t worker_idx;
};

struct TimerFdHandlerContext {
    int32_t timerfd;
    uint32_t worker{};
    uint64_t expire_count{};
};

struct SignalHandlerContext {
    int32_t sigfd;
};

using PollHandlerContext = std::variant<WorkerResponseHandlerContext, TimerFdHandlerContext, SignalHandlerContext>;

template <typename... Visitors> struct PollHandlerContextVisitor : Visitors... {
    using Visitors::operator()...;
};

void send_thread_pkg(void* socket, const ThreadPackage& pkg) {
    using scratch_pad_type = std::vector<std::byte, short_alloc<std::byte, 2048>>;
    scratch_pad_type::allocator_type::arena_type arena{};

    scratch_pad_type scratch_buffer{arena};
    auto serializer = zpp::bits::out(scratch_buffer);
    if (const auto s_result = serializer(pkg); zpp::bits::failure(s_result) || scratch_buffer.empty()) {
        LOG_ERROR(g_logger, "Failed to serialize package: {}", std::make_error_code(s_result.code).message());
        return;
    }

    zmq_msg_t z_msg;
    WRAP_ZMQ_FUNC(zmq_msg_init_size, &z_msg, scratch_buffer.size());
    memcpy(zmq_msg_data(&z_msg), scratch_buffer.data(), scratch_buffer.size());
    SCOPED_GUARD([&z_msg]() { WRAP_ZMQ_FUNC(zmq_msg_close, &z_msg); });
    const int32_t send_res = WRAP_ZMQ_FUNC(zmq_msg_send, &z_msg, socket, ZMQ_DONTWAIT);
    if (send_res == -1) {
        LOG_ERROR(g_logger, "Failed to send thread pkg, error = {}", zmq_strerror(zmq_errno()));
    }
}

tl::optional<ThreadPackage> recv_thread_pkg(void* socket) {
    using scratch_pad_type_t = std::vector<std::byte, short_alloc<std::byte, 2048>>;
    scratch_pad_type_t::allocator_type::arena_type stack_arena{};

    scratch_pad_type_t scratch_buffer{stack_arena};

    for (;;) {
        zmq_msg_t rmsg;
        WRAP_ZMQ_FUNC(zmq_msg_init, &rmsg);
        SCOPED_GUARD([&rmsg]() { WRAP_ZMQ_FUNC(zmq_msg_close, &rmsg); });

        const int32_t recv_res = WRAP_ZMQ_FUNC(zmq_msg_recv, &rmsg, socket, ZMQ_DONTWAIT);
        if (recv_res == -1) {
            if (zmq_errno() != EAGAIN) {
                return tl::nullopt;
            }
            break;
        }

        const size_t msg_bytes = zmq_msg_size(&rmsg);
        assert(msg_bytes + scratch_buffer.size() <= 2048);
        std::copy(static_cast<const std::byte*>(zmq_msg_data(&rmsg)),
                  static_cast<const std::byte*>(zmq_msg_data(&rmsg)) + msg_bytes, std::back_inserter(scratch_buffer));

        if (!zmq_msg_more(&rmsg))
            break;
    }

    ThreadPackage pkg;
    auto deserializer = zpp::bits::in(scratch_buffer);
    if (const auto deserialize_result = deserializer(pkg); zpp::bits::failure(deserialize_result)) {
        LOG_ERROR(g_logger, "deserialization error: {}", std::make_error_code(deserialize_result.code).message());
        return tl::nullopt;
    }

    return tl::optional<ThreadPackage>{pkg};
}

std::atomic_uint32_t g_pixels_processed;

struct UIOptions {
    uint32_t fill_mode{GL_FILL};
};

struct UILogic {
    UIOptions opts;

    void do_ui(UIContext* uictx, const uint32_t pixels_raytraced, const uint32_t pixels_total);
};

void UILogic::do_ui(UIContext* uictx, const uint32_t pixels_raytraced, const uint32_t pixels_total) {
    struct nk_context* ctx = uictx->ctx;
    static nk_colorf bg{.r = 0.10f, .g = 0.18f, .b = 0.24f, .a = 1.0f};

    char scratch_buffer[1024];

    if (nk_begin(ctx, "OpenGL Demo", nk_rect(50, 50, 640, 480),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {

        nk_layout_row_dynamic(ctx, 32, 2);
        if (nk_option_label(ctx, "Fill solid", opts.fill_mode == GL_FILL)) {
            opts.fill_mode = GL_FILL;
        }
        if (nk_option_label(ctx, "Fill wireframe", opts.fill_mode == GL_LINE)) {
            opts.fill_mode = GL_LINE;
        }

        nk_prog(ctx, pixels_raytraced, pixels_total, false);
        auto fmt_res = fmt::format_to(scratch_buffer, "Pixels ({}/{})", pixels_raytraced, pixels_total);
        *fmt_res.out = 0;
        nk_label_colored(ctx, scratch_buffer, NK_TEXT_ALIGN_LEFT, nk_color{0, 255, 0, 255});
        nk_prog(ctx, g_pixels_processed.load(), pixels_total, false);

        // nk_layout_row_static(ctx, 32, 256, 1);

        // for (size_t i = 0, count = draws_data.size(); i < count; ++i) {
        //     auto itr = fmt::format_to_n(scratch_buffer, size(scratch_buffer), "{}", geometry_nodes[i].name);
        //     *itr.out = 0;
        //
        //     if (const auto value = nk_check_label(ctx, scratch_buffer, nodes_visibility[i]);
        //         value != nodes_visibility[i]) {
        //         flush_draw_cmds = true;
        //         nodes_visibility[i] = value != nk_false;
        //     }
        // }
    }
    nk_end(ctx);
}

struct RayTracingWorkPackage {
    glm::u16vec2 pixels_start;
    glm::u16vec2 pixels_end;
};

struct MonkaGigaQueue {
    std::vector<RayTracingWorkPackage> mgq_queue;
    std::mutex mgq_mtx;

    tl::optional<RayTracingWorkPackage> pop_pkg() {
        std::lock_guard qlock{mgq_mtx};
        if (!mgq_queue.empty()) {
            const RayTracingWorkPackage pkg = mgq_queue.back();
            mgq_queue.pop_back();
            return tl::optional<RayTracingWorkPackage>{pkg};
        }
        return tl::nullopt;
    }

    void push_packages(std::vector<RayTracingWorkPackage>&& pkgs) {
        std::lock_guard qlock{mgq_mtx};
        mgq_queue = std::move(pkgs);
    }
};

using RayTracingWorkStealingQueueType = atomic_queue::AtomicQueue2<RayTracingWorkPackage, 4096>;

struct RayTracingWorker {
    MonkaGigaQueue* _work_queue{};
    std::shared_ptr<RayTracingCore> _rtcore{};
    uint32_t _workerid{};
    void* _zmq_channel{};
    void* _zmq_poller{};
    RandomNumberGenerator _randgen{};

    void worker_loop();
    void process_tracing_work_package(const RayTracingWorkPackage& pkg);
};

void RayTracingWorker::worker_loop() {
    SCOPED_GUARD([this]() { WRAP_ZMQ_FUNC(zmq_poller_remove, _zmq_poller, _zmq_channel); });
    SCOPED_GUARD([this]() { WRAP_ZMQ_FUNC(zmq_close, _zmq_channel); });

    int32_t poll_timeout{};
    const int32_t polled_objs_count = WRAP_ZMQ_FUNC(zmq_poller_size, _zmq_poller);
    if (polled_objs_count <= 0) {
        LOG_ERROR(g_logger, "Worker {}, wrong polled objects count {}", _workerid, polled_objs_count);
        return;
    }

    std::byte scratchbuffer[2048];
    assert(std::size(scratchbuffer) >= static_cast<size_t>(polled_objs_count) * sizeof(zmq_poller_event_t));
    MemoryArena scratch_arena{scratchbuffer};

    for (bool quit_flag = false; !quit_flag;) {
        std::vector<zmq_poller_event_t, SimpleArenaAllocator<zmq_poller_event_t>> events_buffer{
            static_cast<size_t>(polled_objs_count), scratch_arena};

        const int32_t polled_events_count = WRAP_ZMQ_FUNC(zmq_poller_wait_all, _zmq_poller, events_buffer.data(),
                                                          static_cast<int32_t>(events_buffer.size()), poll_timeout);

        if (polled_events_count > 0) {
            for (const zmq_poller_event_t& polled_event :
                 std::span<zmq_poller_event_t>{events_buffer.data(), static_cast<size_t>(polled_events_count)}) {
                if (!polled_event.events & ZMQ_POLLIN)
                    continue;

                recv_thread_pkg(_zmq_channel).map([&](const ThreadPackage& pkg) {
                    std::visit(VariantVisitor{
                                   [](const ThreadMessageA& msg_a) {
                                       const ThreadMessageA* a = &msg_a;
                                       LOG_INFO(g_logger, "Worker {} got msg A : .x = {}, .y = {}, .str = {}", 0, a->x,
                                                a->y, a->str);
                                   },
                                   [](const ThreadMessageB& msg_b) {
                                       const ThreadMessageB* b = &msg_b;
                                       LOG_INFO(g_logger, "Worker {} got msg B: .a = {}, .b = {}, .msg = {} ", 0, b->a,
                                                b->b, b->msg);
                                   },
                                   [&quit_flag, this](const ThreadQuitMessage) mutable {
                                       LOG_INFO(g_logger, "Worker {} shutting down ...", _workerid);
                                       quit_flag = true;
                                   },
                                   [](const WorkerResponse&) {},
                                   [](const RaytracedPixel) {},
                               },
                               pkg);
                });
            }
        }

        if (quit_flag)
            break;

        _work_queue->pop_pkg().map_or_else(
            [this, &poll_timeout](RayTracingWorkPackage work_pkg) {
                process_tracing_work_package(work_pkg);
                poll_timeout = 0;
            },
            [&poll_timeout]() { poll_timeout = std::clamp(poll_timeout + 25, 0, 500); });
    }
}

void RayTracingWorker::process_tracing_work_package(const RayTracingWorkPackage& rtpkg) {
    for (uint16_t y = rtpkg.pixels_start.y; y < rtpkg.pixels_end.y; ++y) {
        for (uint16_t x = rtpkg.pixels_start.x; x < rtpkg.pixels_end.x; ++x) {
            const RGBAColor pixel_color = _rtcore->raytrace_pixel(x, y, _randgen);
            send_thread_pkg(this->_zmq_channel, RaytracedPixel{
                                                    .rtp_x = x,
                                                    .rtp_y = y,
                                                    .rtp_color = pixel_color.color,
                                                });
            g_pixels_processed += 1;
        }
    }
}

struct RayTracingWorkerContext {
    std::thread rtwc_thread;
    void* rtwc_channel_from_worker;
};

class RayTracer {
private:
    struct PrivateConstructionToken {};

public:
    RayTracer(PrivateConstructionToken, glm::u16vec2 img_size, const uint32_t poll_count, void* zmq_ctx,
              void* zmq_poller, std::vector<RayTracingWorkerContext> worker_ctx,
              std::unique_ptr<MonkaGigaQueue> work_queue)
        : _imgsize{img_size}, _poll_count{poll_count}, _zmq_context{zmq_ctx}, _zmq_poller{zmq_poller},
          _workqueue{std::move(work_queue)}, _worker_context{std::move(worker_ctx)} {}

    ~RayTracer();
    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    RayTracer(RayTracer&& rhs) noexcept
        : _imgsize{rhs._imgsize}, _pixels_raytraced{rhs._pixels_raytraced}, _poll_count{rhs._poll_count},
          _zmq_context{std::exchange(rhs._zmq_context, nullptr)}, _zmq_poller{std::exchange(rhs._zmq_poller, nullptr)},
          _workqueue{std::move(rhs._workqueue)}, _worker_context{std::move(rhs._worker_context)} {}

    static tl::optional<RayTracer> create();
    void update(RayTracedImageDisplay* img_output);
    void shutdown();
    uint32_t pixels_count() const noexcept { return _imgsize.x * _imgsize.y; }
    uint32_t pixels_raytraced() const noexcept { return _pixels_raytraced; }
    glm::u16vec2 image_size() const noexcept { return _imgsize; }

private:
    glm::u16vec2 _imgsize;
    uint32_t _pixels_raytraced{};
    uint32_t _poll_count;
    void* _zmq_context;
    void* _zmq_poller;
    std::unique_ptr<MonkaGigaQueue> _workqueue;
    std::vector<RayTracingWorkerContext> _worker_context;
};

RayTracer::~RayTracer() {
    std::ranges::for_each(_worker_context, [this](RayTracingWorkerContext& worker) {
        worker.rtwc_thread.join();
        WRAP_ZMQ_FUNC(zmq_poller_remove, _zmq_poller, worker.rtwc_channel_from_worker);
        WRAP_ZMQ_FUNC(zmq_close, worker.rtwc_channel_from_worker);
    });

    WRAP_ZMQ_FUNC(zmq_poller_destroy, &_zmq_poller);
    WRAP_ZMQ_FUNC(zmq_ctx_term, _zmq_context);
}

template <typename T>
    requires std::is_integral_v<T>
T round_up(const T value, const T multiple) noexcept {
    return ((value + multiple - 1) / multiple) * multiple;
}

tl::optional<RayTracer> RayTracer::create() {
    int32_t z_major{};
    int32_t z_minor{};
    int32_t z_patch{};
    zmq_version(&z_major, &z_minor, &z_patch);

    LOG_INFO(g_logger, "ZMQ version {}.{}.{}", z_major, z_minor, z_patch);

    void* ctx_main = WRAP_ZMQ_FUNC(zmq_ctx_new);
    if (!ctx_main) {
        return tl::nullopt;
    }

    void* poller = WRAP_ZMQ_FUNC(zmq_poller_new);
    if (!poller) {
        return tl::nullopt;
    }

    std::shared_ptr<RayTracingCore> rtsetup = RayTracingCore::default_setup();
    const glm::u16vec2 img_size{rtsetup->rts_img_width, rtsetup->rts_img_height};
    const glm::uvec2 rounded_img_size{round_up<uint32_t>(img_size.x, 8), round_up<uint32_t>(img_size.y, 8)};

    const auto cpus = std::min<uint32_t>(std::thread::hardware_concurrency(), 8);
    constexpr uint32_t BLOCK_SIZE = 8;

    const glm::uvec2 pkgs_count = rounded_img_size / uint32_t{8};
    std::vector<RayTracingWorkPackage> work_queue_pkgs{};
    work_queue_pkgs.reserve(pkgs_count.x * pkgs_count.y);
    for (uint32_t y = 0; y < pkgs_count.y; ++y) {
        for (uint32_t x = 0; x < pkgs_count.x; ++x) {
            work_queue_pkgs.push_back(RayTracingWorkPackage{
                .pixels_start =
                    glm::u16vec2{std::min<uint16_t>((x * 8), img_size.x), std::min<uint16_t>((y * 8), img_size.y)},
                .pixels_end = glm::u16vec2{std::min<uint16_t>((x + 1) * 8, img_size.x),
                                           std::min<uint16_t>((y + 1) * 8, img_size.y)},
            });
        }
    }

    std::random_device rnddev{};
    std::mt19937 randgen{rnddev()};
    std::ranges::shuffle(work_queue_pkgs, randgen);

    std::unique_ptr<MonkaGigaQueue> work_queue{std::make_unique<MonkaGigaQueue>()};
    work_queue->push_packages(std::move(work_queue_pkgs));

    std::latch workers_rdy{cpus};

    std::vector<RayTracingWorkerContext> worker_ctx{};

    for (uint32_t idx = 0; idx < cpus; ++idx) {
        void* main_thread_endpoint = WRAP_ZMQ_FUNC(zmq_socket, ctx_main, ZMQ_CHANNEL);
        const std::tuple<int32_t, int32_t> socket_opts[] = {{ZMQ_RCVHWM, 1024 * 1024}, {ZMQ_LINGER, 0}};

        for (const auto [sock_opt, sock_opt_val] : socket_opts) {
            if (const int32_t res =
                    WRAP_ZMQ_FUNC(zmq_setsockopt, main_thread_endpoint, sock_opt, &sock_opt_val, sizeof(sock_opt_val));
                res != 0) {
                return tl::nullopt;
            }
        }

        char tmp_buffer[128];
        auto fres = fmt::format_to(tmp_buffer, "inproc://worker#{}", idx);
        *fres.out = 0;

        if (const int32_t bind_res = WRAP_ZMQ_FUNC(zmq_bind, main_thread_endpoint, tmp_buffer); bind_res != 0) {
            return tl::nullopt;
        }

        if (const int32_t add_res =
                WRAP_ZMQ_FUNC(zmq_poller_add, poller, main_thread_endpoint, reinterpret_cast<void*>(idx), ZMQ_POLLIN);
            add_res != 0) {
            return tl::nullopt;
        }

        worker_ctx.emplace_back(
            std::thread{[&workers_rdy, wqueue = work_queue.get(), idx, rtsetup, ctx_main]() {
                RayTracingWorker worker;
                worker._workerid = idx;
                worker._rtcore = rtsetup;
                worker._work_queue = wqueue;

                {
                    SCOPED_GUARD([&workers_rdy]() { workers_rdy.count_down(); });

                    char tmp_buffer[128];
                    auto fres = fmt::format_to(tmp_buffer, "inproc://worker#{}", idx);
                    *fres.out = 0;

                    worker._zmq_channel = WRAP_ZMQ_FUNC(zmq_socket, ctx_main, ZMQ_CHANNEL);
                    const std::tuple<int32_t, int32_t> socket_opts[] = {{ZMQ_SNDHWM, 1024 * 1024}, {ZMQ_LINGER, 0}};

                    for (const auto [sock_opt, sock_opt_val] : socket_opts) {
                        if (const int32_t res = WRAP_ZMQ_FUNC(zmq_setsockopt, worker._zmq_channel, sock_opt,
                                                              &sock_opt_val, sizeof(sock_opt_val));
                            res != 0) {
                            return;
                        }
                    }
                    if (const int32_t conn_res = WRAP_ZMQ_FUNC(zmq_connect, worker._zmq_channel, tmp_buffer);
                        conn_res != 0) {
                        return;
                    }

                    worker._zmq_poller = WRAP_ZMQ_FUNC(zmq_poller_new);
                    if (!worker._zmq_poller) {
                        return;
                    }

                    if (const int32_t add_res =
                            WRAP_ZMQ_FUNC(zmq_poller_add, worker._zmq_poller, worker._zmq_channel, nullptr, ZMQ_POLLIN);
                        add_res != 0) {
                        return;
                    }
                }

                worker.worker_loop();
            }},
            main_thread_endpoint);
    }

    const int32_t polled_objects = WRAP_ZMQ_FUNC(zmq_poller_size, poller);
    if (polled_objects <= 0) {
        return tl::nullopt;
    }

    workers_rdy.wait();

    return tl::optional<RayTracer>{
        tl::in_place,
        PrivateConstructionToken{},
        img_size,
        static_cast<uint32_t>(polled_objects),
        ctx_main,
        poller,
        std::move(worker_ctx),
        std::move(work_queue),
    };
}

void RayTracer::update(RayTracedImageDisplay* img_output) {
    std::byte scratchbuffer[2048];
    MemoryArena scratch_arena{scratchbuffer};

    assert(std::size(scratchbuffer) >= _poll_count * sizeof(zmq_poller_event_t));
    std::vector<zmq_poller_event_t> polled_events{_poll_count};

    const int32_t events_to_process =
        WRAP_ZMQ_FUNC(zmq_poller_wait_all, _zmq_poller, polled_events.data(), static_cast<int32_t>(_poll_count), 0);

    if (events_to_process <= 0)
        return;

    for (int32_t i = 0; i < events_to_process; ++i) {
        const zmq_poller_event_t& poll_ev = polled_events[i];

        const size_t worker_idx = reinterpret_cast<size_t>(poll_ev.user_data);
        RayTracingWorkerContext* wctx = &_worker_context[worker_idx];

        for (size_t pkg = 0; pkg < 64; ++pkg) {
            if (const tl::optional<ThreadPackage> pkg = recv_thread_pkg(wctx->rtwc_channel_from_worker); pkg) {
                std::visit(VariantVisitor{
                               [](const ThreadMessageA& msg_a) {},
                               [](const ThreadMessageB& msg_b) {},
                               [](const ThreadQuitMessage) {},
                               [](const WorkerResponse&) {},
                               [this, img_output](const RaytracedPixel pixel) {
                                   img_output->write_pixel(pixel.rtp_x, pixel.rtp_y, RGBAColor{pixel.rtp_color});
                                   _pixels_raytraced += 1;
                               },
                           },
                           *pkg);
            } else {
                break;
            }
        }
    }
}

void RayTracer::shutdown() {
    LOG_INFO(g_logger, "Shutting down ...");
    std::ranges::for_each(_worker_context, [](const RayTracingWorkerContext& ctx) {
        LOG_INFO(g_logger, "Stopping worker on channel {}", fmt::ptr(ctx.rtwc_channel_from_worker));
        send_thread_pkg(ctx.rtwc_channel_from_worker, ThreadQuitMessage{});
    });
}

std::byte kScratchBuffer[32 * 1024 * 1024];

int main(int argc, char** argv) {

    // sigset_t signal_set;
    // sigemptyset(&signal_set);
    // for (const int32_t sig : {SIGTERM, SIGINT, SIGQUIT}) {
    //     sigaddset(&signal_set, sig);
    // }

    // const int32_t block_sig_res = eintr_wrap_syscall(sigprocmask, SIG_BLOCK, &signal_set, nullptr);
    // assert(block_sig_res == 0);

    quill::Backend::start();
    // auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_color_sink");
    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
        "raytracer.log",
        []() {
            quill::FileSinkConfig cfg;
            cfg.set_open_mode('w');
            cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);
            return cfg;
        }(),
        quill::FileEventNotifier{});

    quill::PatternFormatterOptions pfo;
    pfo.format_pattern = "%(time) [%(thread_id)] %(source_location:<28) "
                         "LOG_%(log_level:<9) %(logger:<12) %(message)";
    pfo.timestamp_pattern = ("%H:%M:%S.%Qns");
    pfo.timestamp_timezone = quill::Timezone::GmtTime;
    g_logger = quill::Frontend::create_or_get_logger("global_logger", std::move(file_sink));
    g_logger->set_log_level(quill::LogLevel::Debug);

    auto window = PlatformWindow::create();
    if (!window) {
        LOG_ERROR(g_logger, "Failed to create main window!");
        return EXIT_FAILURE;
    }

    auto ui_backend = BackendUI::create(window->handle());
    if (!ui_backend) {
        LOG_ERROR(g_logger, "Failed to create UI backend");
        return EXIT_FAILURE;
    }

    auto raytracer = RayTracer::create();
    if (!raytracer) {
        LOG_ERROR(g_logger, "Failed to create raytracer ...");
        return EXIT_FAILURE;
    }

    auto raytraced_img_display =
        RayTracedImageDisplay::create(window->RenderData.surface_size, raytracer->image_size());
    if (!raytraced_img_display) {
        LOG_ERROR(g_logger, "Failed to create image display!");
        return EXIT_FAILURE;
    }

    MemoryArena main_arena{kScratchBuffer, std::size(kScratchBuffer)};

    struct MainContext {
        UIContext ui_ctx;
        PlatformWindow* win;
        RayTracer* raytracer;
        BackendUI* ui_backend;
        UILogic ui_logic;
        RayTracedImageDisplay* img_display;
        MemoryArena* arena_main;
    } main_ctx = {
        .ui_ctx = {},
        .win = &*window,
        .raytracer = &*raytracer,
        .ui_backend = &*ui_backend,
        .img_display = &*raytraced_img_display,
        .arena_main = &main_arena,
    };

    window->Events.poll_input_start.bind([ctx = &main_ctx](const PlatformWindow::PollInputStartEvent&) {
        ctx->ui_ctx = ctx->ui_backend->new_frame();
        ctx->ui_backend->input_begin();
    });

    window->Events.poll_input_end.bind(
        [ctx = &main_ctx](const PlatformWindow::PollInputEndEvent&) { ctx->ui_backend->input_end(); });

    window->Events.input_event.bind([ctx = &main_ctx](const SDL_Event& event) {
        if (event.type == SDL_EventType::SDL_EVENT_KEY_DOWN) {
            const SDL_KeyboardEvent* e = &event.key;
            if (e->key == SDLK_ESCAPE) {
                ctx->win->set_quit();
            }
        }

        ctx->ui_backend->handle_event(&event);
    });

    window->Events.render_event.bind([main = &main_ctx](const DrawParams& dp) {
        main->raytracer->update(main->img_display);
        main->ui_logic.do_ui(&main->ui_ctx, main->raytracer->pixels_raytraced(), main->raytracer->pixels_count());

        glViewportIndexedf(0, 0.0f, 0.0f, static_cast<float>(dp.surface_width), static_cast<float>(dp.surface_height));

        const float clear_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
        glClearNamedFramebufferfv(0, GL_COLOR, 0, clear_color);
        glClearNamedFramebufferfi(0, GL_DEPTH_STENCIL, 0, 1.0f, 0xff);

        main->img_display->draw();
        main->ui_backend->render(dp);
    });

    window->event_loop();
    main_ctx.raytracer->shutdown();

    // std::string runmode;
    // ProgramOptions prog_opts{};
    // auto cli = lyra::cli{} |
    //            lyra::opt{runmode, "runmode"}["-r"]["--runmode"].required().choices(
    //                [](const std::string& mode) { return mode == "Client" || mode == "Server"; }) |
    //            lyra::opt{prog_opts.port, "port"}["-p"]["--port"].choices(
    //                [](const uint32_t val) { return val < std::numeric_limits<uint16_t>::max(); });
    //
    // if (const auto arg_parse_res = cli.parse({argc, argv}); !arg_parse_res) {
    //     LOG_ERROR(g_logger, "{}", arg_parse_res.message());
    //     return EXIT_FAILURE;
    // }
    //
    // const RunMode run_mode = runmode == "Client" ? RunMode::Client : RunMode::Server;
    // LOG_INFO(g_logger, "Running as: {}, port: {}", runmode, prog_opts.port);
    //
    // int32_t z_major{};
    // int32_t z_minor{};
    // int32_t z_patch{};
    // zmq_version(&z_major, &z_minor, &z_patch);
    //
    // LOG_INFO(g_logger, "ZMQ version {}.{}.{}", z_major, z_minor, z_patch);
    //
    // prog_opts.sigfd = eintr_wrap_syscall(signalfd, -1, &signal_set, SFD_NONBLOCK);
    // if (prog_opts.sigfd == -1) {
    //     LOG_ERROR(g_logger, "signalfd: {}", errno);
    //     return EXIT_FAILURE;
    // }
    // LOG_INFO(g_logger, "Signalfd: {}", prog_opts.sigfd);
    //
    // const int32_t msg_timer = eintr_wrap_syscall(timerfd_create, CLOCK_MONOTONIC, 0);
    // assert(msg_timer != -1);
    // SCOPED_GUARD([msg_timer]() { eintr_wrap_syscall(close, msg_timer); });
    //
    // struct timespec curr_time;
    // eintr_wrap_syscall(clock_gettime, CLOCK_MONOTONIC, &curr_time);
    //
    // const itimerspec timer_interval{
    //     .it_interval = {.tv_sec = 3, .tv_nsec = 0},
    //     .it_value = {.tv_sec = 3, .tv_nsec = curr_time.tv_nsec},
    // };
    //
    // eintr_wrap_syscall(timerfd_settime, msg_timer, 0, &timer_interval, nullptr);
    //
    // void* ctx_main = WRAP_ZMQ_FUNC(zmq_ctx_new);
    // assert(ctx_main != nullptr);
    //
    // void* poller = WRAP_ZMQ_FUNC(zmq_poller_new);
    // assert(poller != nullptr);
    // SCOPED_GUARD([poller]() {
    //     void* p = poller;
    //     zmq_poller_destroy(&p);
    // });
    //
    // std::vector<PollHandlerContext> poll_handler_contexts;
    // std::vector<WorkerState> workers;
    // workers.reserve(kMaxWorkers);
    // for (uint32_t idx = 0; idx < kMaxWorkers; ++idx) {
    //     std::string channel = fmt::format("inproc://worker#{}", idx);
    //     void* z_sock = WRAP_ZMQ_FUNC(zmq_socket, ctx_main, ZMQ_CHANNEL);
    //     const int bind_res = WRAP_ZMQ_FUNC(zmq_bind, z_sock, channel.c_str());
    //     assert(bind_res == 0);
    //
    //     workers.emplace_back(
    //         channel, std::thread{[ctx_main, idx, channel]() {
    //             void* z_sock = WRAP_ZMQ_FUNC(zmq_socket, ctx_main, ZMQ_CHANNEL);
    //             SCOPED_GUARD([z_sock]() { WRAP_ZMQ_FUNC(zmq_close, z_sock); });
    //             WRAP_ZMQ_FUNC(zmq_connect, z_sock, channel.c_str());
    //
    //             void* z_poller = WRAP_ZMQ_FUNC(zmq_poller_new);
    //             WRAP_ZMQ_FUNC(zmq_poller_add, z_poller, z_sock, nullptr, ZMQ_POLLIN);
    //
    //             std::random_device rddev{};
    //             std::mt19937 rng{rddev()};
    //             std::uniform_int_distribution<uint32_t> reply_chance_distrib{0, 100};
    //
    //             for (bool quit = false; !quit;) {
    //                 zmq_poller_event_t poll_evt;
    //                 const int32_t poll_cnt = WRAP_ZMQ_FUNC(zmq_poller_wait, z_poller, &poll_evt, -1);
    //                 if (poll_cnt == -1) {
    //                     continue;
    //                 }
    //
    //                 if (poll_evt.events & ZMQ_POLLIN) {
    //                     recv_thread_pkg(z_sock).map([&](const ThreadPackage& pkg) {
    //                         std::visit(VariantVisitor{
    //                                        [idx](const ThreadMessageA& msg_a) {
    //                                            const ThreadMessageA* a = &msg_a;
    //                                            LOG_INFO(g_logger, "Worker {} got msg A : .x = {}, .y = {},
    //                                            .str =
    //                                            {}",
    //                                                     idx, a->x, a->y, a->str);
    //                                        },
    //                                        [idx](const ThreadMessageB& msg_b) {
    //                                            const ThreadMessageB* b = &msg_b;
    //                                            LOG_INFO(g_logger, "Worker {} got msg B: .a = {}, .b = {},
    //                                            .msg =
    //                                            {}",
    //                                                     idx, b->a, b->b, b->msg);
    //                                        },
    //                                        [&quit, idx](const ThreadQuitMessage) mutable {
    //                                            LOG_INFO(g_logger, "Worker {} shutting down ...", idx);
    //                                            quit = true;
    //                                        },
    //                                        [](const WorkerResponse&) {},
    //                                    },
    //                                    pkg);
    //
    //                         if (const uint32_t reply_chance = reply_chance_distrib(rng); reply_chance >= 85)
    //                         {
    //                             WorkerResponse response;
    //                             response.id = idx;
    //                             response.len = reply_chance;
    //                             auto out =
    //                                 fmt::format_to(response.payload, "worker_to_server_msg: .id = {}, .chance
    //                                 =
    //                                 {}",
    //                                                idx, reply_chance);
    //                             *out.out = 0;
    //
    //                             send_thread_pkg(z_sock, ThreadPackage{response});
    //                         }
    //                     });
    //                 }
    //             }
    //         }},
    //         z_sock);
    //
    //     poll_handler_contexts.emplace_back(WorkerResponseHandlerContext{.worker_idx = idx});
    //     WRAP_ZMQ_FUNC(zmq_poller_add, poller, z_sock, reinterpret_cast<void*>(poll_handler_contexts.size() -
    //     1),
    //                   ZMQ_POLLIN);
    // }
    //
    // poll_handler_contexts.emplace_back(SignalHandlerContext{.sigfd = prog_opts.sigfd});
    // WRAP_ZMQ_FUNC(zmq_poller_add_fd, poller, prog_opts.sigfd,
    // reinterpret_cast<void*>(poll_handler_contexts.size() - 1),
    //               ZMQ_POLLIN);
    //
    // poll_handler_contexts.emplace_back(TimerFdHandlerContext{.timerfd = msg_timer, .worker = 0, .expire_count
    // = 0}); WRAP_ZMQ_FUNC(zmq_poller_add_fd, poller, msg_timer,
    // reinterpret_cast<void*>(poll_handler_contexts.size()
    // - 1),
    //               ZMQ_POLLIN);
    //
    // for (;;) {
    //     zmq_poller_event_t polled_event;
    //     const int32_t evt_count = WRAP_ZMQ_FUNC(zmq_poller_wait, poller, &polled_event, -1);
    //     if (evt_count >= 0) {
    //         const size_t context_index = reinterpret_cast<size_t>(polled_event.user_data);
    //         assert(context_index < poll_handler_contexts.size());
    //         PollHandlerContext& handler_ctx = poll_handler_contexts[context_index];
    //         const bool terminate = std::visit(
    //             PollHandlerContextVisitor{
    //                 [](SignalHandlerContext& sig_ctx) {
    //                     signalfd_siginfo sig_buff[4];
    //                     const ssize_t bytes_read = eintr_wrap_syscall(read, sig_ctx.sigfd, sig_buff,
    //                     sizeof(sig_buff)); if (bytes_read <= 0) {
    //                         return false;
    //                     }
    //
    //                     bool should_quit = false;
    //                     for (ssize_t idx = 0, count = bytes_read /
    //                     (static_cast<ssize_t>(sizeof(signalfd_siginfo)));
    //                          idx < count; ++idx) {
    //                         const signalfd_siginfo& s = sig_buff[idx];
    //                         LOG_INFO(g_logger, "Signal {} {}", s.ssi_signo, s.ssi_code);
    //                         if (s.ssi_signo == SIGINT) {
    //                             LOG_INFO(g_logger, "Got SIGINT ...");
    //                             should_quit = true;
    //                         }
    //                     }
    //                     return should_quit;
    //                 },
    //
    //                 [&workers](TimerFdHandlerContext& timer_ctx) {
    //                     uint64_t counter{};
    //                     const ssize_t bytes_read =
    //                         eintr_wrap_syscall(read, timer_ctx.timerfd, &counter, sizeof(counter));
    //
    //                     if (bytes_read <= 0) {
    //                         return false;
    //                     }
    //
    //                     timer_ctx.expire_count += counter;
    //
    //                     ThreadMessageA msg;
    //                     msg.x = timer_ctx.expire_count;
    //                     msg.y = timer_ctx.worker;
    //                     auto res = fmt::format_to(msg.str, "sv_msg_to_worker: {}", timer_ctx.worker);
    //                     *res.out = 0;
    //
    //                     send_thread_pkg(workers[timer_ctx.worker].z_socket, ThreadPackage{msg});
    //
    //                     timer_ctx.worker = (timer_ctx.worker + 1) % workers.size();
    //                     return false;
    //                 },
    //
    //                 [&workers](WorkerResponseHandlerContext& worker_ctx) {
    //                     WorkerState& w = workers[worker_ctx.worker_idx];
    //
    //                     recv_thread_pkg(w.z_socket).map([](const ThreadPackage& pkg) {
    //                         std::visit(VariantVisitor{
    //                                        [](const ThreadMessageA&) { LOG_ERROR(g_logger, "Unhandled
    //                                        PackageType");
    //                                        },
    //                                        [](const ThreadMessageB&) { LOG_ERROR(g_logger, "Unhandled
    //                                        PackageType");
    //                                        },
    //                                        [](const WorkerResponse& resp) {
    //                                            LOG_INFO(g_logger, "Srv: response from worker {}: .playload =
    //                                            {}",
    //                                                     resp.id, resp.payload);
    //                                        },
    //                                        [](ThreadQuitMessage) {}},
    //                                    pkg);
    //                     });
    //
    //                     return false;
    //                 },
    //             },
    //             handler_ctx);
    //
    //         if (terminate) {
    //             std::ranges::for_each(
    //                 workers, [](WorkerState& w) { send_thread_pkg(w.z_socket,
    //                 ThreadPackage{ThreadQuitMessage{}});
    //                 });
    //             break;
    //         }
    //     }
    // }
    //
    // std::ranges::for_each(workers, [](WorkerState& ws) {
    //     ws.thread.join();
    //     WRAP_ZMQ_FUNC(zmq_close, ws.z_socket);
    // });

    LOG_INFO(g_logger, "Shutting down");

    // if (run_mode == RunMode::Server) {
    //     run_as_server(prog_opts);
    // } else {
    //     run_as_client(prog_opts);
    // }

    return EXIT_SUCCESS;
}
