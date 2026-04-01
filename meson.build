project('ace', 'cpp', default_options: ['cpp_std=c++23'])

# Дериктория с файлами библиотеки
include_dir = include_directories(['include'])

# Подключаем nukes
cmake = import('cmake')
nukes = cmake.subproject('nukes')
nukes = nukes.dependency('nukes')

# Объявляем mimalloc
mimalloc_dep = dependency('mimalloc', fallback : ['mimalloc', 'mimalloc_dep'], required : true)

# Подключаем liburing
liburing_dep = dependency('liburing')

# Декларируем интерфейсную библеотеку
ace_dep = declare_dependency(include_directories: include_dir, dependencies: [ nukes, liburing_dep ])


# # Создаем артифактную библиотеку
# example_lib = library(
#     'example_lib',  # Имя библиотеки
#     include_directories: include_dir,  # Указываем директорию с заголовками
#     install: true,  # Устанавливаем библиотеку (опционально)
# )

# HTTP echo пример
executable(
    'http_echo',
    sources: ['examples/http_echo.cpp'],
    cpp_args: [
        '-std=c++23',
        '-fexperimental-library',
        '-Wno-invalid-offsetof',
    ],
    dependencies: [ace_dep],
)

# Опция компиляции тестов
tests_enabled = get_option('tests')

if tests_enabled
  message('Tests requested')
  gtest_dep = dependency('gtest', main: true, fallback: ['gtest', 'gtest_dep'])
  ace_dep_mimalloc = declare_dependency(include_directories: include_dir, dependencies: [ nukes, mimalloc_dep, liburing_dep ])

  test_sources = files(
    'tests/environment.h',
    'tests/units.h',
    'tests/tests.cpp',
    'tests/main.cpp',
  )

  test_executable = executable (
      'ace_tests',
      sources: test_sources,
      cpp_args: [
        '-std=c++23',
        '-fexperimental-library',
        '-Wno-invalid-offsetof',
#        '-Mmodules',
#        '-fconcepts-diagnostics-depth=2'
      ],
      dependencies: [
          gtest_dep,
          ace_dep_mimalloc,
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
