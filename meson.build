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
    # Объявляем mimalloc
    mimalloc_dep = dependency('mimalloc', fallback : ['mimalloc', 'mimalloc_dep'], required : true)
    gtest_dep = dependency('gtest', main: true, fallback: ['gtest', 'gtest_dep'], required : true)

    test_sources = files(
      'tests/environment.h',
      'tests/tests.cpp',
      'tests/main.cpp',
    )

    # NOTE: Defining compiler-dependent args
    compiler = meson.get_compiler('cpp')
    compiler = compiler.get_argument_syntax()

    compile_args = []
    if compiler == 'gcc'
        compile_args = [
            '-std=c++23',
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

    message('Arguments for @0@ compiler: '.format(compiler), compile_args)

    coverage_enabled = get_option('coverage')
    if coverage_enabled
        message('Coverage mode: --coverage flags added')
        compile_args += ['--coverage', '-O0', '-g']
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
    )

    python = import('python').find_installation('python3')
    tests = run_command(
      python, 'discover_tests.py', test_executable.full_path(),
      check: false
    ).stdout().strip().split()

    message('Found tests: ', tests)

    foreach test_name : tests
    message('applying test - ', test_name)
    test(
      test_name,
      test_executable,
      args: [
        '--gtest_filter= "@0@"'.format(test_name),
        '--gtest_print_time=0',
        '--gtest_color=yes',
        '--gtest_catch_exceptions=0',
        '--gtest_break_on_failure=1'
      ]
    )
    endforeach

endif

# Benchmarks compilation option
benchmarks_enabled = get_option('benchmarks')

if benchmarks_enabled
    message('Benchmarks requested')

    benchmark_dep = dependency('google-benchmark',
        fallback: ['google-benchmark', 'google_benchmark_dep'],
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
