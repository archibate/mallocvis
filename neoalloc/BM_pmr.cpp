#include <list>
#include <benchmark/benchmark.h>
#include <memory_resource>

static void BM_list(benchmark::State &s) {
    for (auto _: s) {
        std::list<int> a;
        for (int i = 0; i < 400; i++) {
            a.push_back(i);
        }
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_list)->MinTime(0.1);

static void BM_pmr_list(benchmark::State &s) {
    for (auto _: s) {
        std::pmr::list<int> a;
        for (int i = 0; i < 400; i++) {
            a.push_back(i);
        }
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_pmr_list)->MinTime(0.1);

static void BM_pmr_list_mono(benchmark::State &s) {
    static char buf[400 * 64];
    for (auto _: s) {
        std::pmr::monotonic_buffer_resource mono(buf, sizeof buf, std::pmr::null_memory_resource());
        std::pmr::list<int> a(&mono);
        for (int i = 0; i < 400; i++) {
            a.push_back(i);
        }
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_pmr_list_mono)->MinTime(0.1);

static void BM_pmr_list_unsync(benchmark::State &s) {
    for (auto _: s) {
        std::pmr::unsynchronized_pool_resource pool;
        std::pmr::list<int> a(&pool);
        for (int i = 0; i < 400; i++) {
            a.push_back(i);
        }
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_pmr_list_unsync)->MinTime(0.1);

static void BM_pmr_list_unsync8192(benchmark::State &s) {
    for (auto _: s) {
        std::pmr::pool_options options;
        options.largest_required_pool_block = 0;
        options.max_blocks_per_chunk = 8192;
        std::pmr::unsynchronized_pool_resource pool(options);
        std::pmr::list<int> a(&pool);
        for (int i = 0; i < 400; i++) {
            a.push_back(i);
        }
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_pmr_list_unsync8192)->MinTime(0.1);

static void BM_pmr_list_sync(benchmark::State &s) {
    for (auto _: s) {
        std::pmr::synchronized_pool_resource pool;
        std::pmr::list<int> a(&pool);
        for (int i = 0; i < 400; i++) {
            a.push_back(i);
        }
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_pmr_list_sync)->MinTime(0.1);

static void BM_pmr_list_sync8192(benchmark::State &s) {
    for (auto _: s) {
        std::pmr::pool_options options;
        options.largest_required_pool_block = 0;
        options.max_blocks_per_chunk = 8192;
        std::pmr::synchronized_pool_resource pool(options);
        std::pmr::list<int> a(&pool);
        for (int i = 0; i < 400; i++) {
            a.push_back(i);
        }
        benchmark::DoNotOptimize(a);
    }
}
BENCHMARK(BM_pmr_list_sync8192)->MinTime(0.1);
