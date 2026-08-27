project(
    'ace',
    'cpp',
    default_options: ['cpp_std=c++23'],
    version : '0.9.9'
)

# Declaring lib dir
include_dir = include_directories(['include'])

# Deps list
depends = []

# Fetching nukes
cmake = import('cmake')
nukes = cmake.subproject('nukes')
nukes = nukes.dependency('nukes')
depends += nukes

# Fetching liburing
liburing = dependency('liburing', required : true)
depends += liburing

# Setting macro env data
cdata = configuration_data()
# option = get_option('meson_options_member')
# cdata.set('DEFINED_VAR_NAME', option)
configure_file(
    input: 'config.h.in',
    output: 'ace.config.h',
    configuration: cdata
)

# Framework entry usage
entry_mode = get_option('ace_entry')
if entry_mode
    # Flexible mode — library injects a WEAK main() that bootstraps co_main().
    # User may define int main() (overrides the weak one) or co_main().
    message('ACE entry enabled: weak (supports both main and co_main)')
    ace_entry = static_library('ace_entry',
        'ace_entry.cpp',
        include_directories: include_dir,
        dependencies: depends,
        install: false,
    )
else
    # Traditional mode — library provides NO main().
    # User MUST define int main() themselves.
    message('ACE entry disabled: user provides main')
    ace_entry = []
endif

# Declaring interfacial lib
ace_dep = declare_dependency(
    link_with: ace_entry,
    include_directories: include_dir,
    dependencies: depends,
)
meson.override_dependency('ace', ace_dep)

# Tests compilation option
tests_enabled = get_option('tests')

if tests_enabled
    message('Tests requested')
    test_sanitizers = get_option('test_sanitizers')
    test_leak_detection = get_option('test_leak_detection')
    if test_sanitizers.contains('thread') and test_sanitizers.length() != 1
        error('test_sanitizers=thread must use a separate build from address/undefined')
    endif
    if test_leak_detection == 'enabled' and not test_sanitizers.contains('address')
        error('test_leak_detection=enabled requires address in test_sanitizers')
    endif
    test_sanitizer_options = []
    if test_sanitizers.length() != 0
        test_sanitizer_options = ['b_sanitize=' + ','.join(test_sanitizers)]
    endif
    # Объявляем mimalloc
    mimalloc_dep = dependency('mimalloc', fallback : ['mimalloc', 'mimalloc_dep'], required : true)
    gtest_dep = dependency('gtest', main: true, fallback: ['gtest', 'gtest_dep'], required : true)

    test_fixture_sources = files(
        'tests/arena_fixture.cpp',
        'tests/backup_fixture.cpp',
        'tests/base_fixture.cpp',
        'tests/channel_extra_fixture.cpp',
        'tests/channel_fixture.cpp',
        'tests/compose_extra_fixture.cpp',
        'tests/console_fixture.cpp',
        'tests/context_fixture.cpp',
        'tests/control_block_fixture.cpp',
        'tests/cross_mechanic_fixture.cpp',
        'tests/cutex_extra_fixture.cpp',
        'tests/cutex_fixture.cpp',
        'tests/dispatcher_fixture.cpp',
        'tests/fs_fixture.cpp',
        'tests/future_traits_fixture.cpp',
        'tests/get_runner_fixture.cpp',
        'tests/id_alloc_fixture.cpp',
        'tests/io_any_fixture.cpp',
        'tests/io_buffer_fixture.cpp',
        'tests/io_entity_fixture.cpp',
        'tests/io_hanged_fixture.cpp',
        'tests/moving_average_fixture.cpp',
        'tests/nukes_alignment_fixture.cpp',
        'tests/omniptr_fixture.cpp',
        'tests/promise_traits_fixture.cpp',
        'tests/queue_fixture.cpp',
        'tests/router_slot_fixture.cpp',
        'tests/runner_fixture.cpp',
        'tests/service_fixture.cpp',
        'tests/signal_fixture.cpp',
        'tests/socket_echo_fixture.cpp',
        'tests/spawn_extra_fixture.cpp',
        'tests/spawn_fixture.cpp',
        'tests/timer_fixture.cpp',
        'tests/yield_fixture.cpp',
    )
    test_sources = test_fixture_sources + files(
        'tests/environment.h',
        'tests/main.cpp',
    )

    # NOTE: Defining compiler-dependent args
    cpp_compiler = meson.get_compiler('cpp')
    compiler = cpp_compiler.get_argument_syntax()

    compile_args = []
    if compiler == 'gcc'
        compile_args = [
            '-std=c++23',
            '-fno-sanitize-address-use-after-scope',
#            '-Wno-invalid-offsetof',
#            '-Mmodules',
#            '-fconcepts-diagnostics-depth=2'
        ]
    elif compiler == 'clang'
        compile_args = [
            '-std=c++23',
            '-flto',
#            '-fexperimental-library',
#            '-Wno-invalid-offsetof'
        ]
    endif

    tuple_formatter_probe = '''
        #include <format>
        #include <optional>
        #include <tuple>

        int main() {
            const auto bool_values = std::tuple<bool, bool, bool, bool>{};
            const auto optional_values = std::tuple<
                std::optional<int>, std::optional<int>,
                std::optional<int>, std::optional<int>>{};
            (void)std::format("{}", bool_values);
            (void)std::format("{}", optional_values);
            return 0;
        }
    '''
    has_tuple_formatter = cpp_compiler.compiles(
        tuple_formatter_probe,
        name: 'tuple std::formatter support',
        args: ['-std=c++23'],
    )
    if has_tuple_formatter
        compile_args += ['-DACE_TEST_HAS_TUPLE_FORMATTER=1']
    else
        compile_args += ['-DACE_TEST_HAS_TUPLE_FORMATTER=0']
    endif
    message('Tuple std::formatter support: ', has_tuple_formatter)

    message('Arguments for @0@ compiler: '.format(compiler), compile_args)

    coverage_enabled = get_option('coverage')
    if coverage_enabled
        message('Coverage mode: --coverage flags added')
        compile_args += ['--coverage']
        coverage_link_args = ['--coverage']
    else
        coverage_link_args = []
    endif

    test_executable = executable (
        'ace_tests',
        sources: test_sources,
        cpp_args: compile_args,
        link_args: coverage_link_args,
        dependencies: [
            gtest_dep,
            ace_dep,
            mimalloc_dep
        ],
        override_options: [
            'debug=true',
            'optimization=0',
        ] + test_sanitizer_options,
    )

    python = import('python').find_installation('python3')
    discovery_script = files('discover_tests.py')[0]
    sanitized_test_runner = files('tests/sanitized_test_runner.py')[0]
    sanitized_test_runner_common_args = [
      sanitized_test_runner,
      '--sanitizers', ','.join(test_sanitizers),
      '--leak-mode', test_leak_detection,
      '--probe-executable', test_executable,
    ]
    sanitized_test_runner_args = sanitized_test_runner_common_args + [
      '--',
    ]
    discovered_tests = run_command(
      python, discovery_script, 'discover', test_fixture_sources,
      check: true
    ).stdout().strip().split()

    message('Found tests: ', discovered_tests)

    foreach test_name : discovered_tests
    message('applying test - ', test_name)
    test(
      test_name,
      python,
      args: sanitized_test_runner_args + [test_executable] + [
        '--gtest_filter=@0@'.format(test_name),
        '--gtest_print_time=0',
        '--gtest_color=yes',
        '--gtest_catch_exceptions=0',
        '--gtest_break_on_failure=1'
      ],
      suite: ['ace', 'sanitizer'],
    )
    endforeach

    discovery_unit_tests = files('tests/discover_tests_test.py')[0]
    test(
      'discover_tests.unit',
      python,
      args: [discovery_unit_tests],
      suite: 'ace',
    )

    test(
      'sanitized_test_runner.unit',
      python,
      args: [files('tests/sanitized_test_runner_test.py')[0]],
      suite: 'ace',
    )

    test(
      'ace_tests.discovery_consistency',
      python,
      args: sanitized_test_runner_args + [python, discovery_script, 'verify', test_executable] + test_fixture_sources,
      depends: [test_executable],
      suite: ['ace', 'sanitizer'],
    )

    if test_sanitizers.contains('address')
        test(
          'ace_tests.lsan_capability',
          python,
          args: sanitized_test_runner_common_args + ['--probe-only'],
          depends: [test_executable],
          suite: ['ace', 'sanitizer'],
        )
    endif

    if entry_mode
        entry_fallback_executable = executable(
            'ace_entry_fallback',
            'tests/entry_fallback.cpp',
            cpp_args: compile_args,
            link_args: coverage_link_args,
            dependencies: [ace_dep],
            override_options: [
                'debug=true',
                'optimization=0',
            ] + test_sanitizer_options,
        )
        test(
            'ace_entry.fallback',
            python,
            args: sanitized_test_runner_args + [python, files('tests/check_entry_fallback.py')[0], entry_fallback_executable],
            depends: [entry_fallback_executable],
            suite: ['ace', 'sanitizer'],
        )
    endif

endif

# Benchmarks compilation option
benchmarks_enabled = get_option('benchmarks')

if benchmarks_enabled
    message('Benchmarks requested')

    benchmark_dep = dependency('google-benchmark',
        fallback: ['google-benchmark', 'google_benchmark_dep'],
        default_options: ['tests=disabled'],
        required: true
    )

    benchmark_sources = files(
        'benchmarks/environment.h',
        'benchmarks/benchmarks.cpp',
        'benchmarks/main.cpp',
    )

    compiler = meson.get_compiler('cpp')
    compiler_syntax = compiler.get_argument_syntax()

    benchmark_compile_args = []
    if compiler_syntax == 'gcc'
        benchmark_compile_args = ['-std=c++23']
    elif compiler_syntax == 'clang'
        benchmark_compile_args = ['-std=c++23', '-flto']
    endif

    benchmark_executable = executable(
        'ace_benchmarks',
        sources: benchmark_sources,
        cpp_args: benchmark_compile_args,
        dependencies: [
            benchmark_dep,
            ace_dep,
        ],
        override_options: [
            'debug=false',
            'optimization=3',
            'b_ndebug=true',
        ],
    )

    benchmark(
        'ace_benchmarks',
        benchmark_executable,
        args: [
            '--benchmark_min_time=0.1s',
            '--benchmark_color=true',
            '--benchmark_counters_tabular=true',
        ]
    )
endif
