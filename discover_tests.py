import subprocess
import sys

# Запускаем тестовый исполняемый файл с флагом --gtest_list_tests
executable = sys.argv[1]
output = subprocess.check_output([executable, '--gtest_list_tests']).decode('utf-8')

# Парсим вывод и регистрируем тесты
tests = []
current_suite = ''
for line in output.splitlines():
    if not line.startswith('  '):
        current_suite = line.strip()
    else:
        test_name = line.strip()
        tests.append(f'{current_suite}{test_name}')

# Выводим список тестов
for test in tests:
    print(test)
