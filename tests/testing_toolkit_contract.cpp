#include <atomic>
#include <chrono>
#include <type_traits>
#include <vector>

#include <ace/ace.h>
#include <ace/core/tools/queue.h>
#include <ace/core/tools/testing_toolkit.h>
#include <ace/futures/timeout.h>
#include <ace/services/kernelic.h>

namespace core = ace::core;
namespace services = ace::services;
namespace tools = ace::core::tools;

// A separate target compiles this contract with and without NDEBUG, including at O0.
template <typename T> concept worker_hook = requires { T::set_worker_start_for_testing(nullptr); };
template <typename T> concept service_hook = requires { T::set_respawn_for_testing(nullptr); };
template <typename T> concept slab_hook = requires { T::set_growth_for_testing(nullptr); };
template <typename T> concept wheel_hook = requires { T::set_initialization_for_testing(nullptr); };
template <typename T> concept ring_hook = requires { T::set_queue_init_for_testing(nullptr); };
template <typename T> concept arena_snapshot = requires(const T& arena) { arena.stats(); };
template <typename T> concept arena_counter = requires { T::live_system_chunks; };
template <typename T> concept node_counter = requires { T::outstanding_bytes(); };
template <typename T> concept transient_counter = requires(T& release) { release._malloc_count; };
template <typename T> concept malloc_allocate_note = requires(T& release) { release.note_malloc_allocate(); };
template <typename T> concept malloc_deallocate_note = requires(T& release) { release.note_malloc_deallocate(); };
template <typename T> concept malloc_snapshot = requires(const T& release) { release.malloc_count(); };
template <typename T> concept pool_allocate_note = requires(T& arena) { arena.note_pool_allocate(64); };
template <typename T> concept pool_deallocate_note = requires(T& arena) { arena.note_pool_deallocate(64); };
template <typename T> concept drain_note = requires(T& arena) { arena.note_drain(); };
template <typename T> concept pool_snapshot = requires(const T& arena) { arena.pool_held(); };
template <typename T> concept drain_snapshot = requires(const T& arena) { arena.drains(); };

template <typename Selected, typename Toolkit, typename Owner>
consteval bool toolkit_contract() {
    static_assert(std::is_base_of_v<tools::testing_mixin<Toolkit>, Toolkit>);
    static_assert(std::is_base_of_v<Selected, Owner>);
    static_assert(std::is_same_v<Selected, Toolkit> == is_debug);
    if constexpr (not is_debug)
        static_assert(std::is_empty_v<Selected>);
    return true;
}

static_assert(worker_hook<core::dispatcher> == is_debug);
static_assert(service_hook<services::clock> == is_debug);
static_assert(service_hook<services::kernel_controller> == is_debug);
static_assert(slab_hook<tools::slab_mempool<int>> == is_debug);
static_assert(wheel_hook<services::hierarchical_time_wheel> == is_debug);
static_assert(ring_hook<services::kernel_controller> == is_debug);
static_assert(arena_snapshot<core::arena> == is_debug);
static_assert(arena_counter<core::arena> == is_debug);
static_assert(node_counter<core::nukes_node_arena> == is_debug);
static_assert(transient_counter<core::extern_release> == is_debug);

// B83: accounting helpers must disappear too; empty release stubs would hide the bug.
static_assert(malloc_allocate_note<core::extern_release> == is_debug);
static_assert(malloc_deallocate_note<core::extern_release> == is_debug);
static_assert(malloc_snapshot<core::extern_release> == is_debug);
static_assert(pool_allocate_note<core::arena> == is_debug);
static_assert(pool_deallocate_note<core::arena> == is_debug);
static_assert(drain_note<core::arena> == is_debug);
static_assert(pool_snapshot<core::arena> == is_debug);
static_assert(drain_snapshot<core::arena> == is_debug);

static_assert(toolkit_contract<core::dispatcher_testing::debug_tools,
    core::dispatcher_testing, core::dispatcher>());
static_assert(toolkit_contract<tools::slab_mempool_testing::debug_tools,
    tools::slab_mempool_testing, tools::slab_mempool<int>>());
static_assert(toolkit_contract<services::hierarchical_time_wheel_testing::debug_tools,
    services::hierarchical_time_wheel_testing, services::hierarchical_time_wheel>());
static_assert(toolkit_contract<services::kernel_controller_testing::debug_tools,
    services::kernel_controller_testing, services::kernel_controller>());
static_assert(toolkit_contract<core::arena_testing::debug_tools,
    core::arena_testing, core::arena>());
static_assert(toolkit_contract<core::extern_release_testing::debug_tools,
    core::extern_release_testing, core::extern_release>());
static_assert(toolkit_contract<core::nukes_node_arena_testing::debug_tools,
    core::nukes_node_arena_testing, core::nukes_node_arena>());
using clock_base = core::traits::service_traits_testing<services::clock, core::service_spawn_mode::e_thread_local>::debug_tools;
using clock_tools = core::traits::service_traits_testing<services::clock, core::service_spawn_mode::e_thread_local>;
static_assert(toolkit_contract<clock_base, clock_tools, services::clock>());

// CRTP selection occurs while this type is incomplete and must never construct it.
struct nonconstructible_testing : tools::testing_mixin<nonconstructible_testing> {
    nonconstructible_testing() = delete;
    int value;
};
static_assert(std::is_same_v<nonconstructible_testing::debug_tools, nonconstructible_testing> == is_debug);
static_assert(std::is_empty_v<nonconstructible_testing::debug_tools> != is_debug);
// Each toolkit retains a distinct selected base even when instrumentation is disabled.
static_assert(not std::is_same_v<core::arena_testing::debug_tools,
                                core::extern_release_testing::debug_tools>);

struct completion : services::kernel_observer {
    int result = -1;
    void on_result(int value) override { result = value; }
};

ace::task smoke_task(std::atomic_size_t& completed) {
    co_await ace::futures::timeout(std::chrono::milliseconds(1));
    ++completed;
}

int main() {
    // Exercise normal allocation, scheduler and timer paths in both modes.
    tools::slab_mempool<int> pool;
    tools::queue<int> queue(pool);
    for (int i = 0; i < 1025; ++i) queue.enqueue(int {i});
    for (int i = 0; i < 1025; ++i)
        if (queue.dequeue() != i) return 1;
    auto& arena = core::arena::get_instance();
    auto* small = arena.allocate(64);
    auto* large = arena.allocate(8192);
    arena.deallocate(small, 64);
    arena.deallocate(large, 8192);
    auto* node = core::nukes_node_arena::allocate(256, 128);
    core::nukes_node_arena::deallocate(node, 256, 128);

    ace::cfg::g_config._runners_amount = 2;
    if (not ace::reload()) return 2;
    std::atomic_size_t completed = 0;
    for (int i = 0; i < 64; ++i) ace::schedule(smoke_task(completed));
    ace::run();
    if (completed != 64 or not ace::empty()) return 3;
    ace::cfg::g_config._runners_amount = 1;
    if (not ace::reload()) return 4;

    // A real ring verifies release calls liburing directly; failure remains an error.
    if (not services::kernel_controller::available()) return 5;
    completion observer;
    if (not services::kernel_controller::nop(&observer)) return 6;
    ace::run();
    return observer.result == 0 ? 0 : 7;
}
