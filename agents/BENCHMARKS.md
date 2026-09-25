# ACE Framework - Benchmarking Guide

Дата актуализации: 2026-09-25.

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
| `benchmarks/benchmarks.cpp` | 26 numbered benchmark scenarios (BM1-BM26). |

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
| BM23 | `bm_legacy_weighted_selection` | Изолированный baseline прежней weighted-selection формулы для 2/4/8/16/64 runners. |
| BM24 | `bm_repeated_short_run` | Повторные schedule/run циклы с 0/1/10/100 задачами и 1/2/4/8/16 runners. |
| BM25 | `bm_dynamic_mpsc_queue`, `bm_dynamic_mpmc_queue` | Direct concurrent queue/reclamation throughput: MPSC 1P/1C и 4P/1C, MPMC 1P/1C и 4P/4C по 16384 сообщений на producer. |
| BM26 | `bm_intrusive_queue_move` | Move construction intrusive queue для 0/1/64/1024/16384 nodes; 256 moves за timed iteration, allocation и FIFO cleanup вне измерения. |

## B75: dynamic Nukes queue reclamation (2026-08-30)

Baseline и result измерены release-сборкой GCC 16.2.1 (`-O3`, `NDEBUG`) на одном
host (12 logical CPU, L3 32 MiB), пятью повторами с медианами и
`--benchmark_min_time=0.05s`. BM25 исключает создание worker threads из timed
interval, но включает concurrent transfer и join; checksum превращает loss или
duplication в benchmark error, а `UseRealTime()` делает throughput производным
от wall time всего transfer. JSON: `/tmp/ace-b75-baseline-direct.json`,
`/tmp/ace-b75-baseline-mpsc.json`, `/tmp/ace-b75-baseline-mpmc11.json`,
`/tmp/ace-b75-baseline-mpmc44.json` и `/tmp/ace-b75-current-direct.json`.

| Сценарий | Baseline median real | Current median real | Изменение |
|----------|---------------------:|--------------------:|----------:|
| MPSC 1P/1C | 0.979 ms | 0.436 ms | -55.5% |
| MPSC 4P/1C | 4/5 повторов завершились loss/duplication; единственный успешный 5.57 ms | 3.63 ms, 5/5 корректно | correctness восстановлен; успешный sample -34.8% |
| MPMC 1P/1C | 0.668 ms | 0.471 ms | -29.5% |
| MPMC 4P/4C | 6.85 ms | 5.58 ms | -18.5% |

Интеграционный контроль тем же способом: BM7 `bm_channel_push_pull` улучшился
с 3.94 до 2.95 ms (-25.1%), BM15 `bm_channel_pending_push` — с 1.405 до
1.31 ms (-6.7%). BM11 после корректировки source quota дал 13.1/23.9/31.8/35.1
ms для 1/4/16/64 runners; это сопоставимо с финальным B50/N8 диапазоном при иной
системной нагрузке и не показывает возврата линейного O(N) selection path.

## B50/N8: load-aware selection и persistent workers (2026-08-30)

Baseline и current измерены одной release-сборкой GCC 16.2.1 (`-O3`, `NDEBUG`),
на одном host (12 logical CPU, L3 32 MiB), по пять повторов с медианами и
`--benchmark_min_time=0.05s`. JSON: `/tmp/ace-b50-n8-baseline.json` и
`/tmp/ace-b50-n8-current-final.json`. Baseline load average был 0.42/0.26/0.43,
current — 2.55/2.40/2.03, поэтому небольшие различия следует считать шумом.

BM11, 200k задач, median baseline → current:

| Runners | Real, ms | CPU, ms | Real change |
|--------:|---------:|--------:|------------:|
| 1 | 13.1 → 12.6 | 12.0 → 12.6 | -3.8% |
| 2 | 17.3 → 14.0 | 15.7 → 13.4 | -19.1% |
| 4 | 26.5 → 22.6 | 19.3 → 21.7 | -14.7% |
| 8 | 33.8 → 26.2 | 20.0 → 25.5 | -22.5% |
| 16 | 43.2 → 29.7 | 20.1 → 25.5 | -31.3% |
| 64 | 75.3 → 33.6 | 22.0 → 27.0 | -55.4% |

BM24 показывает устранение thread-startup/1 ms floor. Median real time для
пустого `run()`: 1053 → 0.007 us (1 runner), 2080 → 0.066 us (2),
3411 → 0.228 us (4), 6302 → 0.733 us (8), 11877 → 1.74 us (16).
Для 100 задач: 2058 → 5.11 us, 3087 → 11.5 us, 4756 → 13.3 us,
7599 → 18.7 us и 14581 → 25.6 us соответственно.

BM23 также уточнил исходный диагноз: при согласованных положительных velocity
прежний accumulator обычно пересекает threshold на втором runner-е
(`selected_runners=2` для всех N), поэтому фактический normal path не рос
линейно. Однако формула оставалась индексно смещённой, data-racy и имела O(N)
fallback при несогласованных метриках. Новый power-of-two-choices path всегда
читает ровно две load-метрики и выполняет O(1) selection/update.

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


## Проверка исправлений B52/B77/B79 (2026-09-21)

Использованы существующие BM9, BM11, BM16, BM24 и BM25; новые сценарии не
понадобились. Baseline: ACE `39de353` и Nukes `04e7cf0` с недостающим
`serialized_freelist.h` из прежнего dependency patch. Result: текущие
исправления и чистый опубликованный Nukes
`7fe452b2054f97c0ec3d707dfd934b5474fcc1fe`, без локального patch.
Оба бинарника собраны GCC 16.2.1, Google Benchmark 1.8.4, `-O3`, `NDEBUG`,
без sanitizers на одном host: 12 logical CPU, L3 32 MiB. Во время измерений
сборки и test suites не выполнялись; частота CPU не фиксировалась.

Выполнены четыре последовательные серии baseline/result/result/baseline,
каждая с `--benchmark_min_time=0.05s --benchmark_repetitions=5`.
Таблица показывает медиану десяти измерений каждой версии; для BM25 — wall
clock, для остальных — CPU time Google Benchmark.

| Сценарий | Baseline | Result | Разница времени |
|----------|----------|--------|-----------------|
| BM9, 20k таймеров по 1 ms | 3.251 ms | 3.248 ms | −0.1% |
| BM11, 200k tasks, 1 runner | 12.50 ms | 12.61 ms | +0.9% |
| BM11, 200k tasks, 4 runners | 21.80 ms | 20.35 ms | −6.7% |
| BM11, 200k tasks, 16 runners | 23.76 ms | 23.92 ms | +0.6% |
| BM24, 1 task, 1 runner | 0.06594 µs | 0.06564 µs | −0.5% |
| BM24, 1 task, 4 runners | 0.6314 µs | 0.6250 µs | −1.0% |
| BM25, MPSC 1P/1C | 0.4235 ms | 0.4304 ms | +1.6% |
| BM25, MPSC 4P/1C | 3.428 ms | 3.418 ms | −0.3% |
| BM25, MPMC 1P/1C | 0.4677 ms | 0.4657 ms | −0.4% |
| BM25, MPMC 4P/4C | 5.651 ms | 5.672 ms | +0.4% |

Заметного устойчивого замедления в этих нагрузках не обнаружено. Разница
BM11/4 не доказывает ускорение: медианы двух baseline-серий менялись
22.84 → 20.62 ms при result 20.38 → 20.33 ms. Для throughput concurrent
scheduler CPU time не заменяет wall-clock latency. BM16 после исправления
прошёл пять повторов без ошибки quiescence: медиана 9.61 ms wall / 9.24 ms CPU
на 20k циклов миграции. Свежесобранный baseline BM16 дал ошибки корректности
во всех пяти повторах (четыре `Dispatcher not empty after reattach test`, один
`Could not gather two distinct runners`), поэтому сравнение скорости миграции
с ним не приводится. Benchmark не заменяет correctness regressions B77.

Команда сравнения (выполняется отдельно для каждого бинарника):

```bash
./ace_benchmarks \
  '--benchmark_filter=^(bm_schedule_throughput/(1|4|16)|bm_repeated_short_run/(1|4)/1|bm_timeout_short|bm_dynamic_m(pmc|psc)_queue/.*)$' \
  --benchmark_min_time=0.05s --benchmark_repetitions=5 \
  --benchmark_out=results.json --benchmark_out_format=json
./ace_benchmarks --benchmark_filter='^bm_reattach_migration$' \
  --benchmark_min_time=0.05s --benchmark_repetitions=5
```


## B80: исключение test-only instrumentation из release (2026-09-22)

Использованы существующие BM9, BM11, BM24 и BM25, без добавления сценариев.
Baseline — чистый ACE `e09db55`; result — B80. Nukes одинаковый в обеих
версиях: `7fe452b2054f97c0ec3d707dfd934b5474fcc1fe`.
GCC 16.2.1, Google Benchmark 1.8.4, `-O3`, `NDEBUG`, без sanitizers;
один host, 12 logical CPU, L3 32 MiB. Параллельные сборки и тесты завершены
до измерений. Частота и CPU affinity вручную не фиксировались.

Четыре последовательные серии baseline/result/result/baseline по пять
повторов: `--benchmark_min_time=0.05s --benchmark_repetitions=5`.
Таблица показывает медиану десяти измерений каждой версии; для BM25 — wall
clock, для остальных — CPU time Google Benchmark.

| Сценарий | Baseline | Result | Разница времени |
|----------|----------|--------|-----------------|
| BM9, 20k таймеров по 1 ms | 3.498 ms | 3.348 ms | −4.3% |
| BM11, 200k tasks, 1 runner | 12.531 ms | 12.468 ms | −0.5% |
| BM11, 200k tasks, 4 runners | 20.226 ms | 21.138 ms | +4.5% |
| BM11, 200k tasks, 16 runners | 23.560 ms | 23.736 ms | +0.8% |
| BM24, 1 task, 1 runner | 0.063 us | 0.064 us | +0.5% |
| BM24, 1 task, 4 runners | 0.625 us | 0.637 us | +2.0% |
| BM25, MPSC 1P/1C | 0.443 ms | 0.440 ms | −0.7% |
| BM25, MPSC 4P/1C | 3.548 ms | 3.315 ms | −6.6% |
| BM25, MPMC 1P/1C | 0.471 ms | 0.478 ms | +1.5% |
| BM25, MPMC 4P/4C | 5.833 ms | 5.749 ms | −1.4% |

Все повторы завершились без benchmark errors. Устойчивое изменение скорости
не установлено: например, BM11/4 дал медианы отдельных серий
20.10/20.17/21.96/20.65 ms. Проценты вычислены до округления значений таблицы.
Отсутствие диагностических atomic RMW в release подтверждается кодом и
проверкой символов, а не заявлением об ускорении на основании шумных чисел.

JSON: `/tmp/ace-toolkit-perf-{1-baseline,2-current,3-current,4-baseline}.json`.
Команда для каждого binary:

```bash
./ace_benchmarks \
  '--benchmark_filter=^(bm_schedule_throughput/(1|4|16)|bm_repeated_short_run/(1|4)/1|bm_timeout_short|bm_dynamic_m(pmc|psc)_queue/.*)$' \
  --benchmark_min_time=0.05s --benchmark_repetitions=5 \
  --benchmark_out=results.json --benchmark_out_format=json
```


### Перенос выбора toolkit в CRTP (2026-09-22)

Общий `core::tools::testing_mixin<Toolkit>` заменяет восемь повторяющихся
consteval selectors и namespace aliases на унаследованный `debug_tools`.
Этот шаг меняет только compile-time выбор базы, не добавляет state и не меняет
тела runtime hooks или allocation/scheduler paths. Новые benchmarks и повторные
измерения не требуются; числа раздела B80 относятся к предшествующему изменению,
а не к новому замеру CRTP-версии. Debug/release contracts проверяют выбранные
типы и отсутствие instrumentation в release.

### B83: восстановление release compilation (2026-09-25)

Обращения к optional toolkit members перенесены в generic lambdas с зависимыми
именами. Исправление сохраняет algorithms, allocation/release protocol,
счётчики debug и отсутствие instrumentation в release. Новые benchmark-сценарии
и замеры не нужны: меняется корректность compile-time lookup. Проверки GCC/Clang
при `-O0` и отсутствие hook/counter storage symbols в release описаны в
`TESTING.md`; выводов об изменении производительности не делаем.

### B45: move intrusive queue (2026-09-25)

Добавлен BM26 `bm_intrusive_queue_move`: один поток, без запуска ACE runners,
0/1/64/1024/16384 заранее созданных nodes. Одна timed iteration выполняет 128
round trips (256 move constructions); allocation и финальное FIFO-drain находятся
вне timed loop. `DoNotOptimize` и `ClobberMemory` сохраняют наблюдаемость moves.
Проверка FIFO не заменяет regressions self-removal в `queue_fixture`.

Baseline — рабочее дерево с B83 до изменения move-конструктора B45; текущая
версия отличается от него только перепривязкой owners и Doxygen. Benchmark
source одинаковый. GCC 16.2.1, C++23, `-O3 -DNDEBUG`, Google Benchmark 1.8.4
(release), без sanitizers; AMD Ryzen 5 7500F, 12 logical CPU, L3 32 MiB.
Affinity/frequency вручную не фиксировались. Сборки и тесты были завершены до
замеров. Четыре последовательные серии baseline/current/current/baseline по
пять повторов, `--benchmark_min_time=0.05s --benchmark_repetitions=5`.

Медианы десяти CPU-time измерений каждой версии; время iteration делится на
256, чтобы получить ns на один move. `items_per_second` уже считает moves.

| Nodes | Baseline, ns/move | B45, ns/move |
|------:|-----------------:|------------:|
| 0 | 1.86 | 2.00 |
| 1 | 1.86 | 1.78 |
| 64 | 1.86 | 36.74 |
| 1024 | 1.86 | 912.82 |
| 16384 | 1.86 | 16352.53 |

Все повторы завершились без benchmark errors. Рост стоимости непустого move
соответствует согласованному переходу O(1) → O(N); baseline не исправлял
`owning_queue`. Малые различия 0/1 node не трактуются как устойчивое ускорение.
Стоимость `q_node::remove()` не менялась, остаётся O(1). Эти измерения не являются
оценкой end-to-end timer performance: в `cascade_slot` выражение `auto&& timers =
std::move(...)` привязывает ссылку и не вызывает move-конструктор очереди.

Для каждого binary выполнена команда:

```bash
LD_LIBRARY_PATH=/home/ivanm/code/cxx/ace/build-bench/subprojects/benchmark-1.8.4 \
  BINARY --benchmark_filter='^bm_intrusive_queue_move/' \
  --benchmark_min_time=0.05s --benchmark_repetitions=5 \
  --benchmark_out=OUTPUT.json --benchmark_out_format=json
```

Baseline binary: `/tmp/ace-b45-baseline/ace_benchmarks`; current binary:
`build-bench/ace_benchmarks`. Данные: `/tmp/ace-b45-perf-{1-baseline,2-current,3-current,4-baseline}.json`.


## B85: контроль automaton ping после исправления cancellation (2026-09-25)

Существующий BM18 `bm_automaton_ping`: один runner, 5000 automatons ×
10 yields + terminal value = 55000 потреблённых значений за iteration.
Новый benchmark не добавлен. Baseline — бинарник после B83/B45, до B85,
сохранённый в `/tmp/ace-b85-baseline/ace_benchmarks`; B84 меняет только тест.
Current собран `meson compile -C build-bench ace_benchmarks -j 2`.
Оба GCC 16.2.1 release (`-O3`, `NDEBUG`), Google Benchmark 1.8.4 release,
AMD Ryzen 5 7500F, 12 logical CPU, L3 32 MiB. Запуски на одном host после
завершения sanitizer tests и сборок; load average 0.92/0.99/0.84.

Четыре последовательные серии baseline/current/current/baseline, каждая
с `--benchmark_filter='^bm_automaton_ping$' --benchmark_min_time=0.05s
--benchmark_repetitions=5`. Для сохранённого бинарника задан
`LD_LIBRARY_PATH=/home/ivanm/code/cxx/ace/build-bench/subprojects/benchmark-1.8.4`.
JSON и логи: `/tmp/ace-b85-perf-{1-baseline,2-current,3-current,4-baseline}.{json,log}`.

| Серия | Median CPU, ms | Median real, ms |
|-------|---------------:|----------------:|
| Baseline 1 | 5.421 | 5.436 |
| Current 2 | 5.553 | 5.570 |
| Current 3 | 5.545 | 5.561 |
| Baseline 4 | 5.551 | 5.571 |

Объединённые 10 samples каждого варианта: median CPU 5.509 → 5.549 ms
(+0.7%), real 5.528 → 5.566 ms (+0.7%). Различие меньше разброса между
baseline-сериями; устойчивое замедление обычного ping не выявлено. BM18 не
измеряет стоимость отмены ожидающего waiter: этот путь проверен correctness
regressions и LSan, а его необходимая работа теперь включает возврат узла
runner-у вместо потери ownership. Вывод о производительности cancellation
из этих измерений не делается.
