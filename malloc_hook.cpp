#include "addr2sym.hpp"
#include "plot_actions.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
#if __unix__
# include <sys/mman.h>
# include <unistd.h>
# define MALLOCVIS_EXPORT
#elif _WIN32
# include <windows.h>
# define MALLOCVIS_EXPORT // __declspec(dllexport)
#endif
#if __cplusplus >= 201703L || __cpp_lib_memory_resource
# include <memory_resource>
#endif
#if __cpp_lib_memory_resource
# define PMR std::pmr
# define PMR_RES(x) \
     { x }
# define HAS_PMR 1
#else
# define PMR std
# define PMR_RES(x)
# define HAS_PMR 0
#endif
#include "alloc_action.hpp"

namespace {

uint32_t get_thread_id() {
#if __unix__
    return gettid();
#elif _WIN32
    return GetCurrentThreadId();
#else
    return 0;
#endif
}

struct alignas(64) PerThreadData {
#if HAS_PMR
    size_t const bufsz = 64 * 1024 * 1024;
# if __unix__
    void *buf = mmap(nullptr, bufsz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# elif _WIN32
    void *buf =
        VirtualAlloc(nullptr, bufsz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
# else
    static char buf[bufsz];
# endif
    std::pmr::monotonic_buffer_resource mono{buf, bufsz};
    std::pmr::unsynchronized_pool_resource pool{&mono};
#endif

    std::recursive_mutex lock;
    PMR::deque<AllocAction> actions PMR_RES(&pool);
    bool enable = false;
};

struct GlobalData {
    std::mutex lock;

    static inline size_t const kPerThreadsCount = 8;
    PerThreadData per_threads[kPerThreadsCount];
    bool export_plot_on_exit = true;
#if HAS_THREADS
    std::thread export_thread;
#endif
    std::atomic<bool> stopped{false};

    GlobalData() {
#if HAS_THREADS
        if (0) {
            std::string path = "malloc.fifo";
            export_thread = std::thread([this, path] {
                get_per_thread(get_thread_id())->enable = false;
                export_thread_entry(path);
            });
            export_plot_on_exit = false;
        }
#endif
        for (size_t i = 0; i < kPerThreadsCount; ++i) {
            per_threads[i].enable = true;
        }
    }

    PerThreadData *get_per_thread(uint32_t tid) {
        return per_threads + ((size_t)tid * 17) % kPerThreadsCount;
    }

#if HAS_THREADS
    void export_thread_entry(std::string const &path) {
# if HAS_PMR
        size_t const bufsz = 64 * 1024 * 1024;
#  if __unix__
        void *buf = mmap(nullptr, bufsz, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#  elif _WIN32
        void *buf = VirtualAlloc(nullptr, bufsz, MEM_RESERVE | MEM_COMMIT,
                                 PAGE_READWRITE);
#  else
        static char buf[bufsz];
#  endif
        std::pmr::monotonic_buffer_resource mono{buf, bufsz};
        std::pmr::unsynchronized_pool_resource pool{&mono};
# endif

        std::ofstream out(path, std::ios::binary);
        PMR::deque<AllocAction> actions PMR_RES(&pool);
        while (!stopped.load(std::memory_order_acquire)) {
            for (auto &per_thread: per_threads) {
                std::unique_lock<std::recursive_mutex> guard(per_thread.lock);
                auto thread_actions = std::move(per_thread.actions);
                guard.unlock();
                actions.insert(actions.end(), thread_actions.begin(),
                               thread_actions.end());
            }
            if (!actions.empty()) {
                for (auto &action: actions) {
                    out.write((char const *)&action, sizeof(AllocAction));
                }
                actions.clear();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (auto &per_thread: per_threads) {
            std::unique_lock<std::recursive_mutex> guard(per_thread.lock);
            auto thread_actions = std::move(per_thread.actions);
            guard.unlock();
            actions.insert(actions.end(), thread_actions.begin(),
                           thread_actions.end());
        }
        if (!actions.empty()) {
            for (auto &action: actions) {
                out.write((char const *)&action, sizeof(AllocAction));
            }
            actions.clear();
        }
    }
#endif

    ~GlobalData() {
        for (size_t i = 0; i < kPerThreadsCount; ++i) {
            per_threads[i].enable = false;
        }
        if (export_thread.joinable()) {
            stopped.store(true, std::memory_order_release);
            export_thread.join();
        }
        if (export_plot_on_exit) {
            std::vector<AllocAction> actions;
            for (size_t i = 0; i < kPerThreadsCount; ++i) {
                auto &their_actions = per_threads[i].actions;
                actions.insert(actions.end(), their_actions.begin(),
                               their_actions.end());
            }
            mallocvis_plot_alloc_actions(std::move(actions));
        }
    }
};

GlobalData *global = nullptr;

struct EnableGuard {
    uint32_t tid;
    bool was_enable;
    PerThreadData *per_thread;

    EnableGuard()
        : tid(get_thread_id()),
          per_thread(global ? global->get_per_thread(tid) : nullptr) {
        if (!per_thread) {
            was_enable = false;
        } else {
            per_thread->lock.lock();
            was_enable = per_thread->enable;
            per_thread->enable = false;
        }
    }

    explicit operator bool() const {
        return was_enable;
    }

    void on(AllocOp op, void *ptr, size_t size, size_t align,
            std::vector<void *> call_stack) const {
        if (ptr) {
            auto now = std::chrono::high_resolution_clock::now();
            int64_t time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               now.time_since_epoch())
                               .count();
            per_thread->actions.push_back(
                AllocAction{op, tid, ptr, size, align, call_stack, time});
        }
    }

    ~EnableGuard() {
        if (per_thread) {
            per_thread->enable = was_enable;
            per_thread->lock.unlock();
        }
    }
};

} // namespace

#if __GNUC__
extern "C" void *__libc_malloc(size_t size) noexcept;
extern "C" void __libc_free(void *ptr) noexcept;
extern "C" void *__libc_calloc(size_t nmemb, size_t size) noexcept;
extern "C" void *__libc_realloc(void *ptr, size_t size) noexcept;
extern "C" void *__libc_reallocarray(void *ptr, size_t nmemb,
                                     size_t size) noexcept;
extern "C" void *__libc_valloc(size_t size) noexcept;
extern "C" void *__libc_memalign(size_t align, size_t size) noexcept;

static std::vector<void*> return_call_stack(){
    std::vector<void*> callStack(1024);
    callStack.resize(backtrace(callStack.data(), callStack.size()));
    return {callStack.begin() + 1, callStack.end()};
}

# define REAL_LIBC(name) __libc_##name
# ifndef MAY_OVERRIDE_MALLOC
#  define MAY_OVERRIDE_MALLOC 1
# endif
# ifndef MAY_OVERRIDE_MEMALIGN
#  define MAY_SUPPORT_MEMALIGN 1
# endif
# undef RETURN_ADDRESS
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 1)
# define RETURN_ADDRESS return_call_stack()
#endif
# ifndef RETURN_ADDRESS
# ifdef __has_builtin
#  if __has_builtin(__builtin_return_address)
#   if __has_builtin(__builtin_extract_return_addr)
#    define RETURN_ADDRESS \
        std::vector<void *>{__builtin_extract_return_addr(__builtin_return_address(0))}
#   else
#    define RETURN_ADDRESS std::vector<void *>{__builtin_return_address(0)}
#   endif
#  endif
# elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
#  define RETURN_ADDRESS std::vector<void *>{__builtin_return_address(0)}
# endif
# endif
# ifndef RETURN_ADDRESS
#  define RETURN_ADDRESS (std::vector<void *>{0})
#  pragma message("Cannot find __builtin_return_address")
# endif
# define CSTDLIB_NOEXCEPT noexcept
#elif _MSC_VER
static void *msvc_malloc(size_t size) noexcept {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void *msvc_calloc(size_t nmemb, size_t size) noexcept {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, nmemb * size);
}

static void msvc_free(void *ptr) noexcept {
    HeapFree(GetProcessHeap(), 0, ptr);
}

static void *msvc_realloc(void *ptr, size_t size) noexcept {
    return HeapReAlloc(GetProcessHeap(), 0, ptr, size);
}

static void *msvc_reallocarray(void *ptr, size_t nmemb, size_t size) noexcept {
    return msvc_realloc(ptr, nmemb * size);
}

# define REAL_LIBC(name) msvc_##name
# ifndef MAY_OVERRIDE_MALLOC
#  define MAY_OVERRIDE_MALLOC 0
# endif
# ifndef MAY_OVERRIDE_MEMALIGN
#  define MAY_SUPPORT_MEMALIGN 0
# endif

# include <intrin.h>

static std::vector<void*> return_call_stack(){
    std::vector<void*> callStack;

    // 获取当前上下文
    CONTEXT context;
    RtlCaptureContext(&context);

    // 初始化 STACKFRAME64
    STACKFRAME64 stackFrame;
    ZeroMemory(&stackFrame, sizeof(STACKFRAME64));

#ifdef _M_IX86
    stackFrame.AddrPC.Offset = context.Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
    stackFrame.AddrPC.Offset = context.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

    // 遍历堆栈
    while (StackWalk64(
#ifdef _M_IX86
        IMAGE_FILE_MACHINE_I386,
#elif _M_X64
        IMAGE_FILE_MACHINE_AMD64,
#endif
        GetCurrentProcess(),
        GetCurrentThread(),
        &stackFrame,
        &context,
        NULL,
        SymFunctionTableAccess64,
        SymGetModuleBase64,
        NULL)) {
        if (stackFrame.AddrPC.Offset == 0) {
            break; // 结束遍历
        }
        callStack.push_back((void*)stackFrame.AddrPC.Offset);
    }

    return {callStack.begin() + 1, callStack.end()};
}

# define RETURN_ADDRESS return_call_stack()
# define CSTDLIB_NOEXCEPT

#else
# define REAL_LIBC(name) name
# ifndef MAY_OVERRIDE_MALLOC
#  define MAY_OVERRIDE_MALLOC 0
# endif
# ifndef MAY_OVERRIDE_MEMALIGN
#  define MAY_SUPPORT_MEMALIGN 0
# endif
# define RETURN_ADDRESS (std::vector<void *>{1})
# define CSTDLIB_NOEXCEPT
#endif

#if MAY_OVERRIDE_MALLOC
MALLOCVIS_EXPORT extern "C" void *malloc(size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *ptr = REAL_LIBC(malloc)(size);
    if (ena) {
        ena.on(AllocOp::Malloc, ptr, size, kNone, RETURN_ADDRESS);
    }
    return ptr;
}

MALLOCVIS_EXPORT extern "C" void free(void *ptr) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::Free, ptr, kNone, kNone, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT extern "C" void *calloc(size_t nmemb,
                                         size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *ptr = REAL_LIBC(calloc)(nmemb, size);
    if (ena) {
        ena.on(AllocOp::Malloc, ptr, nmemb * size, kNone, RETURN_ADDRESS);
    }
    return ptr;
}

MALLOCVIS_EXPORT extern "C" void *realloc(void *ptr,
                                          size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *new_ptr = REAL_LIBC(realloc)(ptr, size);
    if (ena) {
        ena.on(AllocOp::Malloc, new_ptr, size, kNone, RETURN_ADDRESS);
        if (new_ptr) {
            ena.on(AllocOp::Free, ptr, kNone, kNone, RETURN_ADDRESS);
        }
    }
    return new_ptr;
}

MALLOCVIS_EXPORT extern "C" void *reallocarray(void *ptr, size_t nmemb,
                                               size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *new_ptr = REAL_LIBC(reallocarray)(ptr, nmemb, size);
    if (ena) {
        ena.on(AllocOp::Malloc, new_ptr, nmemb * size, kNone, RETURN_ADDRESS);
        if (new_ptr) {
            ena.on(AllocOp::Free, ptr, kNone, kNone, RETURN_ADDRESS);
        }
    }
    return new_ptr;
}

# if MAY_SUPPORT_MEMALIGN
MALLOCVIS_EXPORT extern "C" void *valloc(size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *ptr = REAL_LIBC(valloc)(size);
    if (ena) {
#  if __unix__
        size_t pagesize = sysconf(_SC_PAGESIZE);
#  elif _WIN32
        SYSTEM_INFO info;
        info.dwPageSize = kNone;
        GetSystemInfo(&info);
        size_t pagesize = info.dwPageSize;
#  else
        size_t pagesize = 0;
#  endif
        ena.on(AllocOp::Malloc, ptr, size, pagesize, RETURN_ADDRESS);
    }
    return ptr;
}

MALLOCVIS_EXPORT extern "C" void *memalign(size_t align,
                                           size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *ptr = REAL_LIBC(memalign)(align, size);
    if (ena) {
        ena.on(AllocOp::Malloc, ptr, size, align, RETURN_ADDRESS);
    }
    return ptr;
}

MALLOCVIS_EXPORT extern "C" void *aligned_alloc(size_t align,
                                                size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *ptr = REAL_LIBC(memalign)(align, size);
    if (ena) {
        ena.on(AllocOp::Malloc, ptr, size, align, RETURN_ADDRESS);
    }
    return ptr;
}

MALLOCVIS_EXPORT extern "C" int posix_memalign(void **memptr, size_t align,
                                               size_t size) CSTDLIB_NOEXCEPT {
    EnableGuard ena;
    void *ptr = REAL_LIBC(memalign)(align, size);
    if (ena) {
        ena.on(AllocOp::Malloc, *memptr, size, align, RETURN_ADDRESS);
    }
    int ret = 0;
    if (!ptr) {
        ret = errno;
    } else {
        *memptr = ptr;
    }
    return ret;
}
# endif
#endif

MALLOCVIS_EXPORT void operator delete(void *ptr) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::Delete, ptr, kNone, kNone, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete[](void *ptr) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::DeleteArray, ptr, kNone, kNone, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete(void *ptr,
                                      std::nothrow_t const &) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::Delete, ptr, kNone, kNone, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete[](void *ptr,
                                        std::nothrow_t const &) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::DeleteArray, ptr, kNone, kNone, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void *operator new(size_t size) noexcept(false) {
    EnableGuard ena;
    void *ptr = REAL_LIBC(malloc)(size);
    if (ena) {
        ena.on(AllocOp::New, ptr, size, kNone, RETURN_ADDRESS);
    }
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

MALLOCVIS_EXPORT void *operator new[](size_t size) noexcept(false) {
    EnableGuard ena;
    void *ptr = REAL_LIBC(malloc)(size);
    if (ena) {
        ena.on(AllocOp::NewArray, ptr, size, kNone, RETURN_ADDRESS);
    }
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

MALLOCVIS_EXPORT void *operator new(size_t size,
                                    std::nothrow_t const &) noexcept {
    EnableGuard ena;
    void *ptr = REAL_LIBC(malloc)(size);
    if (ena) {
        ena.on(AllocOp::New, ptr, size, kNone, RETURN_ADDRESS);
    }
    return ptr;
}

MALLOCVIS_EXPORT void *operator new[](size_t size,
                                      std::nothrow_t const &) noexcept {
    EnableGuard ena;
    void *ptr = REAL_LIBC(malloc)(size);
    if (ena) {
        ena.on(AllocOp::NewArray, ptr, size, kNone, RETURN_ADDRESS);
    }
    return ptr;
}

#if (__cplusplus >= 201402L || _MSC_VER >= 1916)
MALLOCVIS_EXPORT void operator delete(void *ptr, size_t size) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::Delete, ptr, size, kNone, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete[](void *ptr, size_t size) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::DeleteArray, ptr, size, kNone, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}
#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))
# if MAY_SUPPORT_MEMALIGN
MALLOCVIS_EXPORT void operator delete(void *ptr,
                                      std::align_val_t align) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::Delete, ptr, kNone, (size_t)align, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete[](void *ptr,
                                        std::align_val_t align) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::DeleteArray, ptr, kNone, (size_t)align, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete(void *ptr, size_t size,
                                      std::align_val_t align) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::Delete, ptr, size, (size_t)align, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete[](void *ptr, size_t size,
                                        std::align_val_t align) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::DeleteArray, ptr, size, (size_t)align, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete(void *ptr, std::align_val_t align,
                                      std::nothrow_t const &) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::Delete, ptr, kNone, (size_t)align, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void operator delete[](void *ptr, std::align_val_t align,
                                        std::nothrow_t const &) noexcept {
    EnableGuard ena;
    if (ena) {
        ena.on(AllocOp::DeleteArray, ptr, kNone, (size_t)align, RETURN_ADDRESS);
    }
    REAL_LIBC(free)(ptr);
}

MALLOCVIS_EXPORT void *operator new(size_t size,
                                    std::align_val_t align) noexcept(false) {
    EnableGuard ena;
    void *ptr = REAL_LIBC(memalign)((size_t)align, size);
    if (ena) {
        ena.on(AllocOp::New, ptr, size, (size_t)align, RETURN_ADDRESS);
    }
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

MALLOCVIS_EXPORT void *operator new[](size_t size,
                                      std::align_val_t align) noexcept(false) {
    EnableGuard ena;
    void *ptr = REAL_LIBC(memalign)((size_t)align, size);
    if (ena) {
        ena.on(AllocOp::NewArray, ptr, size, (size_t)align, RETURN_ADDRESS);
    }
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

MALLOCVIS_EXPORT void *operator new(size_t size, std::align_val_t align,
                                    std::nothrow_t const &) noexcept {
    EnableGuard ena;
    void *ptr = REAL_LIBC(memalign)((size_t)align, size);
    if (ena) {
        ena.on(AllocOp::New, ptr, size, (size_t)align, RETURN_ADDRESS);
    }
    return ptr;
}

MALLOCVIS_EXPORT void *operator new[](size_t size, std::align_val_t align,
                                      std::nothrow_t const &) noexcept {
    EnableGuard ena;
    void *ptr = REAL_LIBC(memalign)((size_t)align, size);
    if (ena) {
        ena.on(AllocOp::NewArray, ptr, size, (size_t)align, RETURN_ADDRESS);
    }
    return ptr;
}
# endif
#endif

#if MANUAL_GLOBAL_INIT
alignas(GlobalData) static char global_buf[sizeof(GlobalData)];

// # if __has_attribute(__constructor__) && __has_attribute(__destructor__)
// #  define GLOBAL_INIT_PRIORITY 101
// # endif

// __attribute__((__constructor__(GLOBAL_INIT_PRIORITY)))
void mallocvis_init() {
    global = new (&global_buf) GlobalData();
}

// __attribute__((__destructor__(GLOBAL_INIT_PRIORITY)))
void mallocvis_deinit() {
    if (global) {
        global->~GlobalData();
        global = nullptr;
    }
}
#else
static GlobalData global_buf;
static int global_init_helper = (global = &global_buf, 0);
#endif
