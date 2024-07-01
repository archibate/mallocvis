#include <list>
#include <benchmark/benchmark.h>
#include <memory_resource>
#include <tbb/parallel_for.h>

static void BM_mt_list(benchmark::State &s) {
    for (auto _: s) {
        tbb::parallel_for((size_t)0, (size_t)100, [] (size_t) {
            std::list<int> a;
            for (int i = 0; i < 400; i++) {
                a.push_back(i);
            }
        });
    }
}
BENCHMARK(BM_mt_list)->MinTime(0.5);

static void BM_mt_pmr_list(benchmark::State &s) {
    for (auto _: s) {
        tbb::parallel_for((size_t)0, (size_t)100, [] (size_t) {
            std::pmr::list<int> a;
            for (int i = 0; i < 400; i++) {
                a.push_back(i);
            }
        });
    }
}
BENCHMARK(BM_mt_pmr_list)->MinTime(0.5);

static void BM_mt_pmr_list_mono(benchmark::State &s) {
    for (auto _: s) {
        tbb::parallel_for((size_t)0, (size_t)100, [] (size_t) {
            static thread_local char buf[400 * 64];
            std::pmr::monotonic_buffer_resource mono(buf, sizeof buf, std::pmr::null_memory_resource());
            std::pmr::list<int> a(&mono);
            for (int i = 0; i < 400; i++) {
                a.push_back(i);
            }
        });
    }
}
BENCHMARK(BM_mt_pmr_list_mono)->MinTime(0.5);

static void BM_mt_pmr_list_unsync(benchmark::State &s) {
    for (auto _: s) {
        tbb::parallel_for((size_t)0, (size_t)100, [] (size_t) {
            std::pmr::unsynchronized_pool_resource pool;
            std::pmr::list<int> a(&pool);
            for (int i = 0; i < 400; i++) {
                a.push_back(i);
            }
        });
    }
}
BENCHMARK(BM_mt_pmr_list_unsync)->MinTime(0.5);

static void BM_mt_pmr_list_sync(benchmark::State &s) {
    for (auto _: s) {
        std::pmr::synchronized_pool_resource pool;
        tbb::parallel_for((size_t)0, (size_t)100, [&] (size_t) {
            std::pmr::list<int> a(&pool);
            for (int i = 0; i < 400; i++) {
                a.push_back(i);
            }
        });
    }
}
BENCHMARK(BM_mt_pmr_list_sync)->MinTime(0.5);
