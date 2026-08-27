# ACE Framework - Benchmarking Guide

Дата актуализации: 2026-08-23.

## Когда нужен бенчмарк

Добавлять или изменять бенчмарк, если задача затрагивает производительность
горячего пути, алгоритмическую сложность, аллокации, contention, планирование,
таймеры, I/O либо масштабирование по runner-ам. Для простой функциональной
ветки без ожидаемого влияния на производительность новый бенчмарк не нужен.

Бенчмарк не заменяет correctness-тест. Нагрузочные тесты, проверяющие инварианты,
остаются в `tests/`; Google Benchmark измеряет только производительность.

## Сборка и запуск

```bash
meson setup build-bench -Dbenchmarks=true
ninja -C build-bench ace_benchmarks
./build-bench/ace_benchmarks
```

Для существующего build-каталога:

```bash
meson setup build-bench --reconfigure -Dbenchmarks=true
ninja -C build-bench ace_benchmarks
./build-bench/ace_benchmarks
```

Бенчмарки используют Google Benchmark. Опция `-Dbenchmarks=true` добавляет
wrap-зависимость `google-benchmark` и цель `ace_benchmarks`. Цель собирается как
release-путь: `debug=false`, `optimization=3` и `b_ndebug=true`; поэтому
`is_debug == false`. `benchmarks/environment.h` содержит compile-time проверку
этого контракта.

## Правила реализации

1. Сначала зафиксировать измеряемый контракт: операция, объём, число runner-ов,
   состояние allocator/runtime и ожидаемая единица результата.
2. Изолировать измеряемую работу от setup/teardown, если подготовка не является
   частью исследуемого пути.
3. Сохранять корректностные проверки результата, но не превращать benchmark в
   единственную защиту инварианта.
4. Для конкурентных сценариев явно задавать число runner-ов и сбрасывать runtime
   между итерациями через общие helpers.
5. Не подгонять объём под желаемый результат. Выбирать нагрузку, которая даёт
   устойчивое измерение и не делает обычный прогон чрезмерно долгим.
6. При оптимизации сравнивать baseline и изменённую версию в одинаковом окружении;
   записывать команду, compiler/build type, CPU и статистически значимые результаты.
7. Не заявлять улучшение по одному шумному прогону. Использовать повторения и
   смотреть распределение, а не только лучшее значение.
8. После добавления или изменения сценария обновить инвентарь ниже и связанные
   сведения в `agents/INDEX.md`.

## Структура

| Файл | Назначение |
|------|-----------|
| `benchmarks/main.cpp` | Google Benchmark entry point. |
| `benchmarks/environment.h` | 4 helpers: `configure_runners`, `reset_runners`, `fetch_into`, `fetch`. |
| `benchmarks/benchmarks.cpp` | 21 benchmark scenarios (BM1-BM20) and 26 named coroutine helpers. |

## Инвентарь

| # | Бенчмарк | Что измеряет |
|---|----------|-------------|
| BM1 | `bm_cutex_race_capture` | Пропускная способность cutex capture/release, 8 runner-ов x 100k. |
| BM2 | `bm_cutex_race_sync` | Cutex с rescheduling и миграцией waiter-ов. |
| BM3 | `bm_timer_parallel` | Масштабируемость clock: 100k таймеров на 4 runner-ах. |
| BM4 | `bm_spawn_cancel` | Массовый spawn и немедленный cancel. |
| BM5 | `bm_timer_ordering` | Доставка таймеров 0..501 ms. |
| BM6 | `bm_multi_runner_cutex` | Целостность счётчика под cutex на 4 runner-ах. |
| BM7 | `bm_channel_push_pull` | Push/pull цикл dynamic bus, 100k сообщений. |
| BM8 | `bm_spawn_join` | Задержка цикла spawn + join. |
| BM9 | `bm_timeout_short` | Пропускная способность clock: 20k таймеров по 1 ms. |
| BM10 | `bm_compose_and`, `bm_compose_or` | Накладные расходы AND/OR-композиций. |
| BM11 | `bm_schedule_throughput` | Attach/yank/release цикл диспетчера, 200k задач. |
| BM12 | `bm_io_buffer_append` | Сборка scatter-gather buffer через append + assemble. |
| BM13 | `bm_io_buffer_clone` | Глубокое копирование `io::buffer`. |
| BM14 | `bm_pipe_io_roundtrip` | Полный write/read roundtrip через io_uring pipe. |
| BM15 | `bm_channel_pending_push` | Асинхронный push с backpressure. |
| BM16 | `bm_reattach_migration` | Cross-runner reattach, 20k переходов. |
| BM17 | `bm_spawn_fire_forget` | Массовый spawn без join, 50k задач. |
| BM18 | `bm_automaton_ping` | Потребление 50k `co_yield` через `ping()`. |
| BM19 | `bm_expire_absolute` | 5k таймеров с абсолютными deadline. |
| BM20 | `bm_compose_variadic` | Variadic AND/OR из трёх и более futures. |

## Происхождение нагрузочных сценариев

Сценарии BM1-BM6 были выделены из медленных correctness-тестов. Исходные тесты
находятся в отдельных fixture-файлах под `tests/`, а объёмы benchmark-версий
подобраны так, чтобы одна итерация обычно занимала около 0.1-0.5 s.

| Исходный тест | Нагрузка | Бенчмарк |
|---------------|----------|----------|
| `tests/timer_fixture.cpp`: `timer_fixture.do_timer_on_runner_parallel_test` | 100k таймеров на 4 runner-ах | `bm_timer_parallel` (BM3) |
| `tests/cutex_fixture.cpp`: `cutex_fixture.cutex_race` | 800k capture/release операций | `bm_cutex_race_capture` (BM1) |
| `tests/cutex_fixture.cpp`: `cutex_fixture.cutex_race_resheduling` | 800k capture/sync операций | `bm_cutex_race_sync` (BM2) |
| `tests/cross_mechanic_fixture.cpp`: `cross_mechanic_fixture.multi_runner_cutex_count` | 16k операций на 4 runner-ах | `bm_multi_runner_cutex` (BM6) |
| `tests/cross_mechanic_fixture.cpp`: `cross_mechanic_fixture.stress_spawn_cancel` | 100 spawn/cancel/join циклов | `bm_spawn_cancel` (BM4) |
| `tests/timer_fixture.cpp`: timer/expire runner tests | 15 длительностей 0..501 ms | `bm_timer_ordering` (BM5) |

## Интерпретация результатов

- Для timer ordering проверять доставку всех deadline, а не монотонность времени:
  соседние длительности могут попасть в один слот time wheel.
- Для многопоточных сценариев учитывать миграции, прогрев allocator-а и состояние
  dispatcher-а между итерациями.
- Для I/O отдельно фиксировать kernel, версию liburing и характеристики устройства.
- Обнаруженный функциональный дефект заносить в `agents/ISSUES.md`; benchmark не
  должен скрывать ошибку или менять ожидаемый контракт ради стабильного числа.

## Проверка и результаты 2026-08-23

- Все 21 сценария успешно собраны и прошли smoke-прогон командой
  `./build/ace_benchmarks --benchmark_min_time=0.001s`; отказов benchmark-сценариев
  не было.
- Полный трёхкратный прогон сохранён в
  `/tmp/opencode/ace-benchmark-current.json`. Он пересекался по времени с
  ASan shuffle-прогоном и выполнялся при load average 5.50, поэтому его результаты
  несопоставимы с baseline.
- Отдельный целевой трёхкратный прогон шести baseline-сценариев сохранён в
  `/tmp/opencode/ace-benchmark-current-targeted.json`; load average составлял
  8.60.

Медианы real time, baseline -> current:

| Бенчмарк | Baseline, ms | Current, ms | Отношение |
|----------|-------------:|------------:|----------:|
| `automaton_ping` | 10.331 | 11.685 | 1.13x |
| `channel_push_pull` | 6.023 | 6.814 | 1.13x |
| `cutex_race_capture` | 296.678 | 1317.024 | 4.44x |
| `pipe_io_roundtrip` | 64.224 | 76.279 | 1.19x |
| `spawn_join` | 5.374 | 6.424 | 1.20x |
| `timer_ordering` | 505.911 | 505.265 | 1.00x |

Из-за различающейся и высокой фоновой нагрузки выводов об изменении
производительности делать нельзя. Текущие числа фиксируют только результат
прогонов; ни один benchmark-сценарий не завершился с ошибкой.

## B13 smoke-проверка 2026-08-27

Исправление B13 восстанавливает ненулевой frame-size metadata, используемый
существующим runner prefetch path. Новый benchmark-сценарий не добавлялся:
BM11 уже измеряет attach/yank/release и вызывает `async::prefetch()`.

Clang 22 release-target успешно собрался, затем
`bm_schedule_throughput` прошёл один smoke iteration с
`--benchmark_min_time=0.01s`: 43.3 ms real, 40.9 ms CPU,
4.88422M items/s при load average 2.73. Это только smoke result без baseline;
вывод о регрессии или улучшении не делается.
