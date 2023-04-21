add_library(contrib-benchmark STATIC)

target_include_directories(contrib-benchmark PUBLIC
    "include"
)

target_sources(contrib-benchmark PRIVATE
    "src/benchmark.cc"
    "src/benchmark_api_internal.cc"
    "src/benchmark_main.cc"
    "src/benchmark_name.cc"
    "src/benchmark_register.cc"
    "src/benchmark_runner.cc"
    "src/check.cc"
    "src/colorprint.cc"
    "src/commandlineflags.cc"
    "src/complexity.cc"
    "src/console_reporter.cc"
    "src/counter.cc"
    "src/csv_reporter.cc"
    "src/json_reporter.cc"
    "src/perf_counters.cc"
    "src/reporter.cc"
    "src/sleep.cc"
    "src/statistics.cc"
    "src/string_util.cc"
    "src/sysinfo.cc"
    "src/timers.cc"
)
