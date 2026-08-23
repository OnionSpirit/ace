# ACE Framework - Issues and Technical Debt

Дата актуализации: 2026-08-23.

Этот файл является единым реестром известных багов, TODO, нестабильностей и
технических нюансов, требующих решения. Закрытые записи не удаляются: их статус
меняется на `Решено`, а в описании сохраняются причина, исправление и регресс-тест.

## Правила ведения

1. Новую проблему добавлять до начала её исправления.
2. Указывать статус, затронутые файлы, симптом, причину или гипотезу, приоритет и
   способ проверки.
3. Использовать статусы `Открыто`, `Исследуется`, `Заблокировано` и `Решено`.
4. После исправления указать суть решения и тест, защищающий от регрессии.
5. Проблемы только регистрировать, если они не являются блокерами текущей задачи. 
   Исправлять не блокирующие проблемы можно только после прямого разрешения пользователя или отдельного запроса.
6. Пробелы тестового покрытия учитывать в `agents/TESTING.md`, а сценарии
   производительности - в `agents/BENCHMARKS.md`.

## Открытые баги

### B11. `cutex::proxy::~proxy()` бросает из `noexcept`-деструктора

- **Статус:** Открыто.
- **Приоритет:** Низкий.
- **Файл:** `include/ace/futures/cutex.h:316-320`.
- **Симптом:** при `sync()`-захвате без ручного `release()` деструктор бросает
  `std::logic_error`; из-за неявного `noexcept` это вызывает `std::terminate()` и
  предупреждение GCC `-Wterminate`.
- **Нюанс:** проверка misuse намеренная, но приложение не может обработать такое
  завершение. Возможные направления: не бросать из деструктора либо выявлять
  неправильное использование до разрушения proxy.
- **Проверка решения:** тест на забытый `release()` не должен аварийно завершать
  процесс и должен подтверждать выбранный контракт API.

### B13. Lambda-coroutine повреждает захваты при `observe()`

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файлы:** coroutine frame/control block в `include/ace/core/async.h` и
  `include/ace/core/traits/promise.h`; текущие нарушения также находились в
  `tests/`, `benchmarks/` и прежних README examples.
- **Симптом:** `observe()` до `schedule()`/`spawn()` у coroutine lambda может
  повредить захваченные ссылки; ASan сообщает heap-use-after-free или
  stack-use-after-scope. Проблема также затрагивает task payload в
  `backup`/`insure`.
- **Причина:** GCC размещает closure в coroutine frame так, что запись поля
  `_block` promise перекрывает захват.
- **Текущий обход:** не использовать coroutine lambdas. Оформлять корутины как
  именованные функции или helper-методы с явными параметрами. Обычные
  некорутинные lambda допустимы.
- **Возможные направления:** явно хранить closure в promise, изменить layout или
  смещение control block либо вынести control block в отдельную аллокацию.
- **Текущая документационная работа:** все coroutine lambdas в tests/benchmarks
  заменяются именованными helpers, но production-дефект остаётся открытым.
- **После решения:** снять соответствующее ограничение в `agents/INDEX.md` и
  `AGENTS.md` и добавить отдельный regression test для control-block layout.

### B14. Повторный полный suite сохраняет глобальное состояние

- **Статус:** Открыто.
- **Приоритет:** Средний.
- **Область:** timer, trace и dispatcher state между повторениями одного процесса.
- **Симптом:** отдельные процессы Meson и shuffle-прогоны стабильны, но repeated
  suite в одном процессе всё ещё может выявлять зависимости от состояния
  предыдущего повторения.
- **Проверка решения:** несколько последовательных полных прогонов в одном
  процессе с фиксированными и случайными seed без зависимости от порядка.

### B23. IPv6 aliases используют IPv4 storage и размеры

- **Статус:** Открыто, исправление отложено.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/net.h`.
- **Симптом:** aliases для `AF_INET6` существуют, но address storage, parsing и
  длины во всех сетевых состояниях используют `sockaddr_in`, `in_addr` и
  `sizeof(sockaddr_in)`; IPv6 API передают ядру несовместимые адреса.
- **Корневая причина:** тип адреса и его размер не зависят от socket domain.
- **Предлагаемое решение:** ввести domain-specific address traits и storage для
  `sockaddr_in6`, корректную цель `inet_pton()`, длины и typed aliases, сохранив
  move-consuming state machine.
- **Проверка решения:** TCP- и UDP-тесты IPv6 loopback, тест invalid address;
  benchmark нужен только если преобразование адресов попадёт в горячий путь.

### B24. `io::link::read_buf()` может потерять накопленные данные при EOF

- **Статус:** Открыто, исправление отложено.
- **Приоритет:** Средний.
- **Файл:** `include/ace/io.h:1886-1905`.
- **Симптом:** после одного или нескольких полных чанков завершающее чтение с
  результатом `0` возвращает `std::unexpected` вместо накопленного буфера.
- **Корневая причина:** цикл проверяет terminal result `bytes_read < 1` и
  завершает корутину до возврата уже собранных чанков.
- **Предлагаемое решение:** хранить состояние накопления отдельно и трактовать
  EOF с учётом уже прочитанных данных, явно зафиксировав контракт пустого EOF.
- **Проверка решения:** regressions для пустого потока, одного чанка и нескольких
  чанков с завершающим EOF.

### B25. I/O lengths сужаются из `size_t` в `unsigned`

- **Статус:** Открыто, исправление отложено.
- **Приоритет:** Средний.
- **Файлы:** `include/ace/io.h`, `include/ace/net.h`,
  `include/ace/services/kernelic.h`.
- **Симптом:** публичные buffer lengths типа `size_t` неявно сужаются до
  `unsigned` в read/write query и kernelic API; большие размеры могут быть
  усечены до отправки в ядро.
- **Корневая причина:** длина не имеет единого типа и проверяемого преобразования
  на границах API.
- **Предлагаемое решение:** выбрать и документировать `size_t`-контракт до
  системного вызова либо выполнять checked conversion/chunking без молчаливого
  усечения.
- **Проверка решения:** boundary tests около `UINT_MAX` без выделения гигантского
  буфера, включая read и write paths.

### B26. Ошибки `bind`/`listen` потребляют исходную entity

- **Статус:** Открыто, исправление отложено; требуется решение по API.
- **Приоритет:** Средний.
- **Файл:** `include/ace/net.h`.
- **Симптом:** неуспешные bind/listen queries уже потребили исходную entity и
  возвращают invalid next-state entity, поэтому retry/recovery невозможны, а
  состояние владения FD неочевидно.
- **Корневая причина:** move-consuming переход выполняется до известного
  результата системной операции, а failure result не возвращает владельца.
- **Предлагаемое решение:** выбрать явный failure result, сохраняющий или
  восстанавливающий entity, либо документировать terminal consumption как
  контракт; изменение требует отдельного API-решения.
- **Проверка решения:** тесты ownership после bind/listen failure, retry согласно
  выбранному контракту и отсутствие double-close.

### B27. Out-of-class definitions `cutex` нарушают ODR и блокируют Clang 22

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файлы:** `include/ace/futures/cutex.h`,
  `tests/cross_mechanic_fixture.cpp`, `tests/cutex_extra_fixture.cpp`,
  `tests/future_traits_fixture.cpp`.
- **Симптом:** header-only заголовок содержит пять не-`inline` определений
  `try_lock`, `notify`, `pending_notify`, `capture` и `release`; split tests
  обходят duplicate symbols через GCC/Itanium `.weak` asm, который Clang 22
  отвергает с `changed binding to STB_GLOBAL`.
- **Корневая причина:** ODR-нарушение маскируется compiler- и ABI-specific asm.
- **Решение:** пять определений помечены `inline`, а `.weak` asm удалён из трёх
  test fixtures. Header-only реализация теперь использует стандартный ODR-safe
  механизм weak/COMDAT emission.
- **Проверка решения:** Clang 22 собрал все четыре затронутых fixture TU;
  `ld -r` объединил их без `STB_GLOBAL` conflict. Полная сборка `ace_tests`
  дошла до независимой ошибки B35 в `spawn_fixture.cpp`.

### B35. Clang не собирает диагностику tuple через `std::format`

- **Статус:** Решено.
- **Приоритет:** Средний.
- **Файл:** `tests/spawn_fixture.cpp:184`.
- **Симптом:** Clang 22 с libstdc++ 16 останавливает сборку на передаче
  `std::tuple<std::optional<int>, ...>` в `ace::println()`; libstdc++ не имеет
  применимого `std::formatter` для tuple.
- **Корневая причина:** диагностический вызов использует `std::format` для типа,
  для которого стандартный C++23 не предоставляет formatter. Это не связано с
  runtime API `spawn` и не относится к исправлению B27.
- **Решение:** Meson выполняет compile-time feature probe для обоих tuple типов и
  передаёт результат через `ACE_TEST_HAS_TUPLE_FORMATTER`. Тестовые `#if` теперь
  зависят от capability-макроса, а не от номера Clang; при отсутствии formatter
  пропускается только необязательная диагностика.
- **Проверка решения:** Clang 22 + libstdc++ 16 корректно получают probe result
  `false`; `ninja -C build ace_tests` успешно компилирует и линкует target.
  Runtime-запуск связанных тестов отдельно выявил независимый `io_uring` null-ring
  failure в окружении, поэтому он не используется как проверка B35.

### B28. Meson принимает argument syntax за compiler identity

- **Статус:** Открыто.
- **Приоритет:** Средний.
- **Файл:** `meson.build:110-132,244-252`.
- **Симптом:** test и benchmark sections сравнивают результат
  `compiler.get_argument_syntax()` с `clang`; Clang возвращает gcc-compatible
  syntax, поэтому clang branch недостижим и его flags не применяются.
- **Корневая причина:** синтаксис аргументов используется как идентификатор
  компилятора.
- **Предлагаемое решение:** сохранить compiler object и выбирать ветку через
  `compiler.get_id()`, используя argument syntax только там, где действительно
  различается формат flags.
- **Проверка решения:** сверить setup logs и фактические test/benchmark compile
  flags в отдельных GCC- и Clang-конфигурациях.

### B29. Некорректный regression блокирует проверку B16

- **Статус:** Открыто, блокирует тестирование edge case B16.
- **Приоритет:** Высокий.
- **Файл:** `tests/yield_fixture.cpp:227-243`.
- **Симптом:**
  `yield_fixture.automaton_join_returns_nullopt_when_pending_yield_was_consumed`
  детерминированно падает 10/10 и оставляет GCC full suite на 291/292.
- **Корневая причина:** тест вызывает `await_resume()` после `await_ready() ==
  false`, не вызывая `await_suspend()`; ожидание pending initial yield также
  противоречит lazy `automaton::initial_suspend`.
- **Предлагаемое решение:** построить корректный детерминированный протокол:
  явно довести raw observed automaton до pending yield и затем чередовать
  join/ping handlers либо использовать runner synchronization с полным await
  protocol. Тест обязан падать без B16 и проходить с ним.
- **Проверка решения:** отдельный повторный прогон edge regression и полный GCC
  suite; текущий тест успешным не считается.

### B30. Transition queries удерживают ссылки на перемещаемые source entities

- **Статус:** Открыто.
- **Приоритет:** Средний/высокий.
- **Файлы и символы:** `include/ace/fs.h` (`fs::file::open_query`,
  `fs::file::open()`); `include/ace/net.h` (bind/connect query types и
  соответствующие transition methods).
- **Симптом:** transition queries сохраняют ссылки или указатели на source entity
  и её storage, тогда как `IMPORT_IO_ENTITY_ENV` теперь публично предоставляет
  default derived moves. Например, `file::open_query` хранит `file&` и указатель
  `_path.c_str()`, а net bind/connect queries хранят ссылки на entity и указатели
  на address storage. Перемещение source после создания outstanding query может
  инвалидировать storage или заставить query потребить moved-from entity; в
  результате возможны возврат валидного FD, помеченного закрытым, либо утечка FD.
- **Предлагаемое решение:** query должен владеть потребляемой entity и стабильным
  address/path storage; альтернатива допустима только как enforceable API-запрет
  перемещения entity, пока существует query.
- **Проверка решения:** fs- и net-тесты создают query, затем перемещают source до
  await и проверяют корректное ownership, результат перехода и строго однократное
  закрытие FD.

### B31. Self-move assignment `fs::file` может изменить path

- **Статус:** Открыто.
- **Приоритет:** Низкий.
- **Файлы и символы:** `include/ace/fs.h` (`fs::file::operator=(file&&)`);
  `include/ace/io.h` (`io::entity::operator=(entity&&)`).
- **Симптом:** default derived move assignment продолжает self-move `_path` после
  того, как base move assignment досрочно вернулся как no-op. Выражение
  `file = std::move(file)` поэтому может оставить path в unspecified state, хотя
  base-класс обещает self-move no-op.
- **Предлагаемое решение:** добавить явную derived self-assignment check либо
  реализовать move assignment, сохраняющий path и base ownership при self-move.
- **Проверка решения:** выполнить self-move `fs::file`, затем открыть тот же path
  и проверить сохранение path, корректный результат и ownership FD.

### B32. Не покрыта отмена owning и direct `close_query`

- **Статус:** Открыто.
- **Приоритет:** Низкий.
- **Файлы и символы:** `include/ace/io.h` (`io::entity::close()`,
  `io::close_query`); `tests/io_entity_fixture.cpp` (close-query coverage).
- **Симптом:** тесты различают entity-owned и напрямую созданный `close_query` при
  await/discard, но не проверяют cancellation задач, приостановленных на этих
  двух путях. Регрессия может нарушить намеренный контракт N4, привести к
  unintended close, повторному закрытию или незавершённому lifetime query.
- **Предлагаемое решение:** добавить отдельные cancellation tests для задачи,
  suspended на entity-owned query, и задачи с напрямую созданным query, не меняя
  различие ownership из N4.
- **Проверка решения:** отменить обе suspended задачи и проверить завершение их
  lifetime, строго однократное закрытие owning FD и отсутствие закрытия direct
  non-owning FD.

### B33. Move-assignment coverage не проверяет `io::link` и net entities

- **Статус:** Открыто.
- **Приоритет:** Низкий.
- **Файлы и символы:** `include/ace/io.h` (`io::link` move assignment, payload и
  guard); `include/ace/net.h` (net entity move assignments, address metadata и
  base delegation); `tests/io_entity_fixture.cpp` (move-assignment coverage).
- **Симптом:** move-assignment tests покрывают только `test_io_entity`, но не
  прямые move/self-move paths `io::link` с payload/guard и net entities с
  metadata/base delegation. Регрессии могут оставить guard привязанным к source,
  потерять адресные данные или нарушить ownership FD.
- **Предлагаемое решение:** добавить прямые move-assignment и self-move tests для
  `io::link` и репрезентативных net entities без изменения production-кода до
  выявления конкретного дефекта.
- **Проверка решения:** проверить освобождение прежнего destination FD, строго
  однократное закрытие принятого FD, сохранение payload/address metadata,
  корректную привязку guard и no-op self-move.

### B34. Нижняя граница timer-теста нестабильна относительно scheduler timing

- **Статус:** Открыто.
- **Приоритет:** Средний.
- **Файлы и символы:** `tests/timer_fixture.cpp`
  (`timer_fixture.do_or_await_test`); `include/ace/services/clock.h`
  (`cached_now()`).
- **Симптом:** в трёхкратном shuffled GCC/ASan-прогоне без блокирующего B29 тест
  один раз измерил 98 ms при `EXPECT_GE(..., 100 ms)`; остальные итерации прошли.
  Поведение `cached_now()` с обновлением на каждом 16-м вызове является намеренным
  контрактом B6 и не должно изменяться или переоткрываться из-за этой записи.
  Нестабильность отлична от B14, хотя может взаимодействовать с process state.
- **Предлагаемое решение:** определить production timing contract либо изменить
  тестовый контракт так, чтобы он использовал внешнее измерение через steady
  clock и ограниченный допуск scheduler/timer, не скрывающий преждевременное
  завершение.
- **Проверка решения:** выполнить много shuffled/repeated прогонов с разными seed
  в GCC/ASan, отдельно и в полном suite без B29, и подтвердить одновременно
  стабильность теста и соблюдение выбранной временной границы.

### B35. Fire-and-forget write fallback теряет command и moved buffer

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файлы и символы:** `include/ace/fs.h` (`file_link::output_action()`),
  `include/ace/net.h` (`connection_link::output_action()`),
  `include/ace/io.h` (`io::outcast::_command_pool`).
- **Симптом:** оба output path сначала захватывают `outcast::command` и перемещают
  в него исходный `io::buffer`. Если `kernel_controller::writev()` или
  `sendmsg()` возвращает `false`, команда не возвращается через `raw_sync()`, а
  blocking fallback вызывает `assemble()` у уже moved-from `buff`. Это теряет
  слот freelist, удерживает payload в недоступной команде и может выполнить
  пустую или некорректную fallback-запись.
- **Причина:** неуспешный submit не принимает ownership observer-а, однако
  fallback-пути, в отличие от `guard::dispatch_close()`, не восстанавливают
  ownership команды и данных.
- **Предлагаемое решение:** при неуспешном submit вернуть command в pool и
  выполнять fallback над буфером, ownership которого явно восстановлен, либо
  выполнять blocking fallback непосредственно из `cmd->_buffer` до возврата
  команды. Зафиксировать единый ownership-контракт helper-ом только при
  согласовании отдельного рефакторинга.
- **Проверка решения:** принудительно заставить submission buffer reject request,
  проверить точные bytes для file/socket fallback, возврат command slot и
  отсутствие удержанного buffer; отдельно проверить успешный async path без
  двойного `raw_sync()`.

### B36. `is_debug` имел инвертированную семантику

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файлы и символы:** `include/ace/core/tools/macro.h` (`is_debug`),
  `include/ace/core/arena.h` (все compile-time условия по флагу).
- **Симптом:** `is_debug` был `true` при определённом `NDEBUG`, то есть в
  release-сборке, а debug-сборка получала `false`. Условия в `arena.h` были
  построены вокруг этой инверсии и не соответствовали имени публичного флага.
- **Решение:** `is_debug` теперь определяется как `true` только при отсутствии
  `NDEBUG`; все arena-условия используют прямую семантику флага. Benchmark
  target явно получает `b_ndebug=true`, поскольку `debug=false` и `-O3` сами по
  себе не определяют `NDEBUG`.
- **Регресс-тест:** `arena_fixture.is_debug_matches_build_configuration`,
  compile-time guard в `benchmarks/environment.h`, debug-сборка и отдельная
  компиляция с `-DNDEBUG` со статической проверкой `is_debug == false`.

## TODO в исходном коде

Все записи ниже имеют статус `Открыто`. Они зафиксированы как технический долг;
их наличие не является разрешением на реализацию.

| ID | Файл | TODO |
|----|------|------|
| T1 | `include/ace/futures/channel.h:415` | Добавить batch read. |
| T2 | `include/ace/core/async.h:55` | Перенести yield operation в generator. |
| T3 | `include/ace/core/dispatcher.h:295` | Вернуть отложенную логику после появления spawn groups. |
| T4 | `include/ace/core/traits/promise.h:73` | Перенести async routers в rules. |
| T5 | `include/ace/core/runner.h:89` | Найти способ сохранить важную валидацию без предупреждения компилятора. |
| T6 | `include/ace/core/runner.h:193` | Добавить поддержку automaton. |
| T7 | `include/ace/core/runner.h:203` | Добавить поддержку automaton во втором пути attach/carrier. |
| T8 | `include/ace/io.h:321` | Улучшить cancel с удалением из локальной submission queue. |
| T9 | `include/ace/core/traits/routing.h:228` | Заменить `memcpy` на копирование только указателя. |

## Открытые технические нюансы

### N1. `nukes::pop_batch()` непригоден для arena release queue

- **Статус:** Открыто.
- **Приоритет:** Низкий.
- **Область:** `include/ace/core/arena.h` и внешняя очередь nukes.
- **Симптом:** iterator batch-а начинает чтение с dummy-node, поэтому первый
  dereference возвращает мусор.
- **Текущий путь:** arena дренирует очередь обычным `pop()` в цикле.
- **Проверка решения:** отдельный тест batch-чтения без чтения dummy-node, затем
  arena cross-thread release tests.

### N2. Arena utilization counter не считает все операции

- **Статус:** Открыто.
- **Приоритет:** Низкий.
- **Файл:** `include/ace/core/arena.h`.
- **Нюанс:** счётчик cadence увеличивается только в ветке формулы утилизации;
  ветки `max == 0` и `occupied == 0` его не меняют. Нумерация не соответствует
  фактическому числу alloc/dealloc операций.
- **Проверка решения:** тест cadence для всех трёх веток с явно заданным числом
  операций.

### N3. Debug arena stats включают служебные аллокации PMR

- **Статус:** Открыто, требуется решение о контракте.
- **Приоритет:** Низкий.
- **Файл:** `include/ace/core/arena.h`.
- **Нюанс:** `live_system_chunks` и `pool_held_bytes` учитывают upstream storage
  `std::pmr::unsynchronized_pool_resource`, включая служебные блоки libstdc++.
  Текущие тесты проверяют относительные дельты, но точный публичный смысл метрик
  не зафиксирован.
- **Проверка решения:** документированный контракт stats и независимые от версии
  стандартной библиотеки тесты.

### N5. Benchmark baseline и current измерены при несопоставимой нагрузке

- **Статус:** Открыто, исследуется.
- **Приоритет:** Средний.
- **Область:** результаты производительности в `agents/BENCHMARKS.md` и файлы
  `/tmp/opencode/ace-benchmark-current.json`,
  `/tmp/opencode/ace-benchmark-current-targeted.json`.
- **Нюанс:** baseline и current запускались при различающейся высокой фоновой
  нагрузке. Особенно заметно отличие BM1 `cutex_race_capture`: медиана real time
  изменилась с 296.678 ms до 1317.024 ms, или в 4.44x. Эти данные не позволяют
  отделить изменение реализации от влияния окружения.
- **Предлагаемое решение:** повторно измерить baseline и current в одном спокойном
  окружении, зафиксировав CPU governor/frequency, compiler, build configuration и
  CPU affinity; выполнить несколько повторений и исследовать распределение, а не
  отдельное значение медианы.
- **Ограничение:** не изменять код на основании текущих несопоставимых данных.
- **Проверка решения:** сопоставимые повторные прогоны baseline/current при
  зафиксированном окружении и анализ распределения результатов всех шести
  сценариев, особенно BM1.

## Зафиксированные технические нюансы

### N4. Ownership напрямую созданного и entity-owned `close_query` различается

- **Статус:** Зафиксировано как намеренный контракт API; открытого дефекта нет.
- **Приоритет:** Информационный.
- **Файл:** `include/ace/io.h`.
- **Нюанс:** напрямую созданный `close_query(fd)` остаётся non-owning и при
  уничтожении без await не закрывает FD; `entity::close()` передаёт sole
  ownership возвращаемому query, поэтому discarded query обязан закрыть FD.
- **Причина:** public query не должен неявно отнимать ownership у вызывающего, а
  entity-owned path должен сохранять единственного владельца после потребления
  entity.
- **Поддержание контракта:** сохранять явные Doxygen и тесты обоих путей.
- **Проверка:** `io_entity_fixture.direct_close_query_discard_remains_non_owning`,
  `io_entity_fixture.entity_close_awaited_single_ownership` и
  `io_entity_fixture.entity_close_discarded_single_ownership`.

## Решённые баги

### B1. `or_await_composed<3+>`: ошибка преобразования `void` в `bool`

- **Статус:** Решено.
- **Файл:** `include/ace/core/compose.h`.
- **Причина:** выражение `_waiter and _waiter->_data` обращалось к `void`-данным
  `ace::task` и попадало в перегруженный future-комбинатор.
- **Решение:** условие использует явные `_waiter.operator bool()` и
  `_waiter->_data.is_exist()`.
- **Тест:** `cross_mechanic_fixture.or_await_composed_3`.

### B2. `and_await_composed`: подозрение на потерю cancel observer-задач

- **Статус:** Решено, дефект не подтвердился.
- **Файл:** `include/ace/core/compose.h`.
- **Результат исследования:** отмена observer-задач уже работала; изменение
  реализации не потребовалось.
- **Тест:** `cross_mechanic_fixture.and_compose_with_cancel`.

### B3. `omniptr::operator&()`: нарушение const-correctness

- **Статус:** Решено.
- **Файл:** `include/ace/core/tools/omniptr.h`.
- **Причина:** `const`-метод возвращал изменяемый `T**` и снимал квалификатор.
- **Решение:** у `operator&()` удалён `const`.
- **Тестовый долг:** регресс-тест `omniptr_fixture.address_of_operator` ещё не
  реализован; он остаётся в `agents/TESTING.md`.

### B4. `async_handle::join()` возвращал `false` для успешной void-корутины

- **Статус:** Решено.
- **Файлы:** `include/ace/core/traits/promise.h`,
  `include/ace/core/async_handle.h`.
- **Причина:** `promise_return_traits<void>::return_void()` не устанавливал
  `e_finished`, тогда как `join()` проверял именно этот статус.
- **Решение:** void-return path устанавливает `e_finished`.

### B5. Move-конструктор `queue` не очищал источник

- **Статус:** Решено.
- **Файл:** `include/ace/core/tools/queue.h`.
- **Причина:** после move исходные `head` и `tail` продолжали ссылаться на узлы.
- **Решение:** указатели источника обнуляются.
- **Тест:** `queue_fixture.queue_move_constructor`.

### B6. `cached_now()` нарушал точность таймеров

- **Статус:** Решено.
- **Файл:** `include/ace/services/clock.h`.
- **Уточнённый контракт:** обновление timestamp каждый 16-й вызов является
  намеренным текущим поведением. Возраст кэша не создаёт отдельного refresh.
- **Статус решения:** запись о дополнительном refresh при возрасте 1 ms была
  устаревшей документацией; production-код изменять не требуется.
- **Проверка:** timer/compose tests проверяют наблюдаемую точность, не полагаясь на
  refresh каждого вызова.

### B7. `channel_router::cancel()` зацикливался

- **Статус:** Решено.
- **Файл:** `include/ace/futures/channel.h`.
- **Причина:** цикл повторно reattach-ил одну ноду, не вызывая следующий
  `pop_node()`.
- **Решение:** на каждой итерации извлекается следующая нода.
- **Тест:** `cross_mechanic_fixture.cancel_spawned_with_channel` переоткрыт и
  стабилен.

### B8. `kernel_controller::submit()` разыменовывал null SQE

- **Статус:** Решено.
- **Файл:** `include/ace/services/kernelic.h`.
- **Причина:** при заполненном ring `io_uring_get_sqe()` возвращал `nullptr`, но
  код безусловно передавал его в `io_uring_sqe_set_data()` и удерживал SQE для
  отложенного применения.
- **Решение:** overflow-запрос буферизуется без SQE; `kernel_entity::apply()`
  получает свежий SQE непосредственно перед применением.
- **Тест:** `base_fixture.kernelic_overflow_buffer_stress` с 6000 запросами.

### B9. `kernel_controller::ping()` терял overflow-запросы

- **Статус:** Решено.
- **Файл:** `include/ace/services/kernelic.h`.
- **Причина:** unsigned underflow в `max_entries - _queries` позволял выкачать
  больше запросов, чем вмещал ring; неуспешный `apply()` терял запрос.
- **Решение:** знаковая граница, результат `apply()` проверяется, запрос при
  нехватке SQE возвращается в очередь.
- **Тест:** `base_fixture.kernelic_overflow_buffer_stress`.

### B10. `ping_handler::await_resume()` игнорировал `yield_value()`

- **Статус:** Решено.
- **Файл:** `include/ace/core/async_handle.h`.
- **Причина:** результат `[[nodiscard]] yield_value()` игнорировался, поэтому при
  гонке возвращалось default-значение вместо `std::nullopt`.
- **Решение:** неуспешное чтение возвращает `std::nullopt`.

### B12. Meson передавал нерабочий `--gtest_filter`

- **Статус:** Решено.
- **Приоритет исходной проблемы:** Критический.
- **Файл:** `meson.build`.
- **Причина:** аргумент содержал пробел и кавычки после `=`, поэтому каждый из 234
  запусков успешно выполнял ноль тестов.
- **Решение:** используется `--gtest_filter=@0@` без пробела и кавычек.

### B15. `fs::file::open_rewrite()` не усекал существующий файл

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/fs.h`.
- **Симптом:** повторная запись более короткого содержимого оставляла старый
  хвост файла.
- **Корневая причина:** `fs::file::open_rewrite()` открывал файл без `O_TRUNC`.
- **Решение:** в flags открытия добавлен `O_TRUNC`.
- **Проверка:** `fs_fixture.open_rewrite_truncates_existing_file`.

### B16. Valued automaton join мог вернуть неинициализированное значение

- **Статус:** Решено в production-коде; edge regression заблокирован B29.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/async_handle.h`.
- **Симптом:** если pending yield был потреблён между проверкой готовности и
  чтением, valued `automaton_join_handler` мог вернуть default или
  неинициализированное значение.
- **Корневая причина:** результат `yield_value()` типа `bool` игнорировался.
- **Решение:** неуспешное чтение yield возвращает `std::nullopt`, после попытки
  чтения active automaton отменяется.
- **Проверка:** стандартные join tests проходят; специальный edge regression из
  B29 некорректен и успешным не считается до замены.

### B17. `io::read_query` писал NUL за пределами точного буфера

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/io.h`.
- **Симптом:** exact-size read выполнял one-byte OOB write, а raw/binary input
  получал неоговорённый терминатор.
- **Корневая причина:** `await_resume()` записывал `NUL` в `buf[_res]`.
- **Решение:** запись терминатора удалена; query возвращает raw bytes без
  изменения данных за фактически прочитанным диапазоном.
- **Проверка:**
  `io_entity_fixture.read_query_exact_buffer_preserves_canary_and_binary_data`.

### B18. `transport::recv(vector/string)` писал в spare capacity

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/net.h`.
- **Симптом:** recv overloads разрешали ядру записывать за logical size объекта,
  используя зарезервированную, но логически отсутствующую область.
- **Корневая причина:** длина вычислялась через `capacity()`.
- **Решение:** vector path передаёт `size() * sizeof(T)`, string path передаёт
  `size()`.
- **Проверка:** `io_entity_fixture.connection_recv_vector_uses_logical_size` и
  `io_entity_fixture.connection_recv_string_uses_logical_size`.

### B19. Move assignment сетевых и I/O entities терял ownership FD

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файлы:** `include/ace/io.h`, `include/ace/net.h`.
- **Симптом:** move assignment мог утечь или потерять destination FD, а guard
  после перемещения мог остаться связан с полями другого объекта.
- **Корневая причина:** прежний destination owner не освобождался, а default/base
  move paths не сохраняли корректную привязку guard.
- **Решение:** старый destination FD освобождается до принятия нового ownership,
  guard привязывается к полям destination, net move paths используют корректный
  base ownership transfer.
- **Проверка:**
  `io_entity_fixture.entity_move_assignment_releases_old_and_keeps_incoming`.

### B20. Self-move assignment инвалидировал единственного владельца FD

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файлы:** `include/ace/io.h`, `include/ace/net.h`.
- **Симптом:** `entity = std::move(entity)` мог закрыть или сбросить собственный
  descriptor.
- **Корневая причина:** move assignment не проверял self-assignment перед
  освобождением destination ownership.
- **Решение:** self-move является no-op.
- **Проверка:** `io_entity_fixture.entity_self_move_preserves_ownership`.

### B21. `entity::close()` не передавал FD возвращаемому query

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/io.h`.
- **Симптом:** entity сохранял FD после `close()`, поэтому discarded/canceled
  queries могли приводить к double-close либо leak; повторный close не был явно
  идемпотентен.
- **Корневая причина:** ownership оставался одновременно связан с entity и
  асинхронным close path.
- **Решение:** `entity::close()` передаёт FD owning query и сразу инвалидирует
  entity; повторный close является no-op, а напрямую созданный `close_query`
  остаётся non-owning согласно N4.
- **Проверка:** `io_entity_fixture.entity_close_awaited_single_ownership`,
  `io_entity_fixture.entity_close_discarded_single_ownership`,
  `io_entity_fixture.entity_close_repeated_is_idempotent` и
  `io_entity_fixture.direct_close_query_discard_remains_non_owning`.

### B22. UDP bind и net move paths нарушали единственное ownership

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/net.h`.
- **Симптом:** UDP bind создавал result из raw FD, не потребляя source; listener
  сохранял self-referential pointer на длину адреса, который устаревал после
  move; отдельные net move paths обходили ownership базового класса.
- **Корневая причина:** state transitions копировали descriptor/address metadata
  вместо согласованного consuming move, а query ссылался на movable member.
- **Решение:** bind и net transitions потребляют source через base moves;
  listener использует address-length member напрямую без stale self-reference.
- **Проверка:** `base_fixture.udp_bind_transfers_sole_ownership` и существующее
  UDP echo coverage `base_fixture.udp_sendto_recv_loop`.

## История flaky-тестов

Все перечисленные нестабильности имеют статус `Решено`. На 2026-08-07 выполнен
21 успешный shuffle-прогон с seed 10..30; полный набор 237/237 проходил без сбоев.

| ID | Тест | Причина и решение |
|----|------|-------------------|
| F1 | `compose_extra_fixture.or_await_left_wins` | Устаревшее время B6; clock исправлен, интервалы разведены. |
| F2 | `cross_mechanic_fixture.interrupt_during_timeout` | Задержка timer service из-за B6; clock исправлен. |
| F3 | `cross_mechanic_fixture.spawn_post_interaction` | Недетерминированные ранние timer events из-за B6; clock исправлен. |
| F4 | `cross_mechanic_fixture.cancel_spawned_with_channel` | Бесконечный cancel-loop B7; тест переоткрыт. |
| F5 | `runner_fixture.suspending_task_run` | Standalone runner не давал 1 ms timer истечь; pump дополнен `sleep_for(2ms)`. |
| F6 | timer/expire ordering tests | Таймеры соседних длительностей могут попасть в один слот; тест проверяет доставку каждого таймера, а не ложную монотонность. |
