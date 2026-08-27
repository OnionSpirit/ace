# ACE Framework - Benchmarking Guide

Дата актуализации: 2026-08-28.

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
| `benchmarks/benchmarks.cpp` | 22 numbered benchmark scenarios (BM1-BM22) and 28 named coroutine helpers. |

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
| BM21 | `bm_connection_link_idle_cancel` | Responsiveness и cancellation для 1/10/100 idle `connection_link` reads. |
| BM22 | `bm_nukes_node_release` | Reuse-path capture/release в local Nukes node pool. |

## Происхождение нагрузочных сценариев

Сценарии BM1-BM6 были выделены из медленных correctness-тестов. Исходные тесты
находятся в отдельных fixture-файлах под `tests/`, а объёмы benchmark-версий
подобраны так, чтобы одна итерация обычно занимала около 0.1-0.5 s.

| Исходный тест | Нагрузка | Бенчмарк |
|---------------|----------|----------|
| `tests/timer_fixture.cpp`: `timer_fixture.do_timer_on_runner_parallel_test` | 1100 таймеров на 4 runner-ах; тяжёлый вариант оставлен benchmark-у | `bm_timer_parallel` (BM3, 100k) |
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

## Direct registration clock: baseline и результат 2026-08-28

Изменение B73 затронуло timer hot path, но существующие BM3/BM5/BM9/BM19 уже
изолируют bulk relative timers, диапазон wheel slots, короткие timers и absolute
deadlines. Новый сценарий не добавлялся. До и после изменения использовалась
release-сборка GCC 16.2.1 (`-O3`, `NDEBUG`), по пять последовательных повторов
каждого сценария с `--benchmark_report_aggregates_only=true`. CPU: 12 logical
threads, L3 32 MiB. Измерения выполнялись при разном load average, поэтому малые
различия real time нельзя трактовать как точную регрессию.

Медианы исходного call-count cache → промежуточного pending-epoch варианта →
финального direct-registration варианта:

| Бенчмарк | Real, ms | CPU, ms | Pending → direct |
|----------|---------:|--------:|------------------|
| BM3 `bm_timer_parallel` | 474.62 → 483 → 477 | 24.80 → 72.1 → 59.0 | Real -1.2%, CPU -18.2% |
| BM5 `bm_timer_ordering` | 503 → 511 → 509 | 29.4 → 31.3 → 45.2 | Real -0.4%; fresh polling reads увеличивают CPU длинного sparse-сценария |
| BM9 `bm_timeout_short` | 7.05 → 10.4 → 9.23 | 4.93 → 5.79 → 5.15 | Real -11.3%, CPU -11.1% |
| BM19 `bm_expire_absolute` | 21.0 → 21.0 → 21.0 | 4.44 → 5.40 → 4.99 | Real без изменения, CPU -7.6% |

Отдельный контрольный прототип менял только тело `cached_now()` на безусловный
`steady_clock::now()`. Он действительно был быстрее pending-epoch решения в
основных relative-сценариях: BM3 475/46.0 ms, BM5 503/44.6 ms и BM9 8.20/4.59 ms
(real/CPU); BM19 дал 26.0/5.33 ms. Однако этот прототип не проходил correctness:
таймеры 256-501 ms завершались примерно на 1.2 ms раньше, 500 us timeout — за
8 us, а отложенный absolute `expire` выбирал неверную ветвь. Поэтому финальный
вариант сохраняет прямые clock reads, но также исправляет deadline calculation.

Финальная схема устраняет двойное enqueue/dequeue и обязательный следующий
epoch: relative timer читает `steady_clock` при регистрации и сразу помещается в
wheel; `ping()` также получает fresh timestamp. Это возвращает большую часть
регрессии промежуточного решения, но строгий deadline contract всё ещё дороже
неточного call-count cache, особенно по CPU в BM3/BM5. Измеренный отдельным
100M-call циклом `steady_clock::now()` стоил около 17.7 ns на этом host.

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

## B54 baseline protocol (2026-08-27)

`BM21` создаёт 1, 10 или 100 `socketpair` peers без данных, запускает reads,
проверяет доставку 1 ms timer и отменяет все reads. Для baseline используется
отдельный worktree от pre-fix commit и ограниченный по времени запуск: blocking
`::recv` не должен завершать idle phase. Изменённый путь должен завершить все
cancellation handles; latency/throughput допустимо сравнивать только в одном
доступном `io_uring` environment. В текущем container runtime-прогон блокирует
B38 до выполнения benchmark-кода.

## B13 smoke-проверка 2026-08-27

Исправление B13 восстанавливает ненулевой frame-size metadata, используемый
существующим runner prefetch path. Новый benchmark-сценарий не добавлялся:
BM11 уже измеряет attach/yank/release и вызывает `async::prefetch()`.

Clang 22 release-target успешно собрался, затем
`bm_schedule_throughput` прошёл один smoke iteration с
`--benchmark_min_time=0.01s`: 43.3 ms real, 40.9 ms CPU,
4.88422M items/s при load average 2.73. Это только smoke result без baseline;
вывод о регрессии или улучшении не делается.
