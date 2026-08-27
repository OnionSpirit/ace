# ACE Framework - Issues and Technical Debt

Дата актуализации: 2026-08-27.

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

- **Статус:** Решено.
- **Приоритет:** Высокий.
- **Файлы:** coroutine frame/control block в `include/ace/core/async.h` и
  `include/ace/core/traits/promise.h`; текущие нарушения также находились в
  `tests/`, `benchmarks/` и прежних README examples.
- **Симптом:** `observe()` до `schedule()`/`spawn()` у coroutine lambda может
  повредить захваченные ссылки; ASan сообщает heap-use-after-free или
  stack-use-after-scope. Проблема также затрагивает task payload в
  `backup`/`insure`.
- **Подтверждённая корневая причина:**
  `promise_traits::operator new()` выделяет `[control_block][coroutine frame]`,
  конструирует `control_block` по адресу `ptr`, но записывает `_frame_size` через
  `static_cast<control_block*>(mem_ptr)`, где `mem_ptr = ptr +
  control_block_size`. Таким образом запись попадает в начало coroutine frame, а
  не в prefix block. Последующая инициализация promise может замаскировать
  повреждение; layout coroutine lambda делает его наблюдаемым на захватах.
- **Ложноположительный прежний тест:**
  `promise_traits_fixture.operator_new_layout` проверял, что `_frame_size` в
  настоящем prefix block равен нулю после eager completion. Именно ноль там и
  оставался из-за ошибочной записи в frame, поэтому тест проходил при наличии
  дефекта и не защищал заявленный layout.
- **Решение:** `promise_traits::operator new()` теперь записывает `_frame_size`
  через указатель на сконструированный prefix block. Значение содержит точный
  полный размер `[control_block][coroutine frame]`, остаётся immutable до
  деаллокации и не используется как старый disown marker. `prefetch()` обходит
  allocation byte offsets с шагом cache line вместо арифметики
  `control_block*`. Intrusive layout и refcount lifecycle уточнены в Doxygen.
- **Регресс-тесты:** `promise_traits_fixture.operator_new_layout` проверяет
  точный ненулевой размер; `operator_new_preserves_frame_canary` падает при
  записи metadata в frame; `observe_preserves_named_coroutine_arguments`,
  `observe_preserves_lambda_coroutine_captures` и
  `observed_lambda_coroutine_cancels_safely` проверяют named baseline и
  lambda-coroutines с value/reference captures при completion и cancellation.
- **API/documentation:** blanket-запрет coroutine lambdas снят из `README.md`,
  `agents/INDEX.md` и `AGENTS.md`. Остаётся стандартное lifetime-требование C++:
  capturing closure обязан жить до completion/cancellation возвращённой
  корутины; немедленный вызов временного capturing closure не получает
  дополнительного lifetime от ACE.
- **Проверка:** pre-fix exact-size и canary regressions падали на Clang 22 +
  ASan. После исправления пять B13 regressions и `async_prefetch` прошли 6/6 на
  GCC 16 и Clang 22 с ASan; весь `promise_traits_fixture` прошёл 20 shuffle
  iterations на каждом compiler. ASan+UBSan recover-прогоны также дали 6/6,
  но clean UBSan остаётся заблокирован dependency issue B68. Полный GCC/ASan
  suite дал 253/297: 43 известных B38 null-ring failures и один B29 failure,
  без B13 regressions.

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

### B24. `read_buf()` может потерять накопленные данные при EOF

- **Статус:** Открыто, исправление отложено.
- **Приоритет:** Средний.
- **Файлы и символы:** `include/ace/io.h` (`io::link::read_buf()`),
  `include/ace/net.h` (`transport_entity::recv_buf()`).
- **Симптом:** после одного или нескольких полных чанков завершающее чтение с
  результатом `0` возвращает `std::unexpected` вместо накопленного буфера.
- **Корневая причина:** обе почти одинаковые реализации проверяют terminal
  result `bytes_read < 1` и завершают корутину до возврата уже собранных чанков.
- **Предлагаемое решение:** хранить состояние накопления отдельно и трактовать
  EOF с учётом уже прочитанных данных, явно зафиксировав контракт пустого EOF.
- **Проверка решения:** одинаковые regressions для file/link и TCP transport:
  пустой EOF, один неполный chunk, ровно один полный chunk + EOF, несколько
  полных chunks + EOF и terminal error после накопленных данных. Зафиксировать,
  когда partial data возвращается как value, а когда ошибка имеет приоритет.

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
- **Дополнительное подтверждение 2026-08-27:** свежий setup с Clang 22.1.8
  вывел `Arguments for gcc compiler` и применил GCC-набор, включая
  `-fno-sanitize-address-use-after-scope`; Clang-ветка с `-flto` осталась
  недостижимой.
- **Предлагаемое решение:** сохранить compiler object и выбирать ветку через
  `compiler.get_id()`, используя argument syntax только там, где действительно
  различается формат flags.
- **Проверка решения:** сверить setup logs и фактические test/benchmark compile
  flags в отдельных GCC- и Clang-конфигурациях.

### B29. Некорректный regression блокирует проверку B16

- **Статус:** Решено 2026-08-27.
- **Приоритет:** Высокий.
- **Файл:** `tests/yield_fixture.cpp:227-243`.
- **Симптом:**
  `yield_fixture.automaton_join_returns_nullopt_when_pending_yield_was_consumed`
  детерминированно падает 10/10 и оставляет GCC full suite на 291/292.
- **Корневая причина:** тест вызывает `await_resume()` после `await_ready() ==
  false`, не вызывая `await_suspend()`; ожидание pending initial yield также
  противоречит lazy `automaton::initial_suspend`.
- **Решение:** regression теперь наблюдает lazy automaton, через публичный
  `awake()` доводит его до pending yield и выполняет два валидных ready/resume
  pairs: competing `ping()` забирает observed value до `join().await_resume()`.
  Последний обязан вернуть `nullopt` и отменить automaton.
- **Проверка:** Clang 22 + ASan — пять shuffled repeats целевого набора; GCC 16
  ASan+UBSan и TSan clean configurations проходят без diagnostics.

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

### B37. Fire-and-forget write fallback теряет command и moved buffer

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
- **Связь с B38:** init-failure path теперь отдельно возвращает command в pool и
  очищает payload до `raw_sync()`; это не исправляет основной сценарий B37 при
  отказе submit после успешной инициализации ring.

### B38. Ошибка инициализации `io_uring` игнорируется и приводит к null-ring crash

- **Статус:** Решено 2026-08-27.
- **Приоритет:** Критический.
- **Файлы и символы:** `include/ace/services/kernelic.h`
  (`kernel_controller::available()`, `initialization_error()`, constructor,
  `submit()`, `ping()`, registration APIs, destructor); `include/ace/io.h`
  (query await path); `include/ace/fs.h`, `include/ace/net.h` (outcast writes);
  `tests/io_entity_fixture.cpp`.
- **Симптом:** конструктор игнорирует отрицательный результат
  `io_uring_queue_init_params()`. После этого первый submit вызывает
  `io_uring_get_sqe()` над неинициализированным ring и падает в
  `io_uring_load_sq_head()` на null address. Свежий
  `context_fixture.do_runner_test` воспроизвёл ASan SEGV через
  `console::println()` -> `file_link::output_action()` -> `writev()`.
- **Причина:** у controller не было состояния `initialized/error`, а destructor,
  submit, ping и registration APIs предполагали успешную инициализацию.
- **Решение:** controller сохраняет thread-local точный init result. Публичные
  `available()` и `initialization_error()` создают controller current thread и
  предоставляют non-throwing status. При failure `submit()` возвращает `false`,
  registration APIs и I/O query возвращают сохранённый negative errno, `ping()`
  не обращается к ring, а destructor освобождает только успешно созданный ring.
  Outcast console/file/socket path очищает command payload, возвращает node в pool
  и сообщает error handler, не переходя к blocking fallback.
- **Регресс-тесты:**
  `io_entity_fixture.kernelic_init_failure_reports_availability_and_rejects_ring_operations`,
  `io_query_returns_kernel_init_error_without_submission` и
  `console_output_reports_kernel_init_error_and_releases_command` используют
  deterministic injected `-EPERM` и проверяют status API, direct APIs, query,
  destructor safety и outcast cleanup. `base_fixture.kernel_controller_nop`
  проверяет successful CQE path при доступном ring, иначе exact negative init
  error without submission.
- **Проверка:** Clang 22 + ASan targeted B38 tests проходят; host-прогон с
  доступным `io_uring` подтверждает 28/28 `io_entity_fixture`, включая 20/20
  повторов migration path. Полный ASan+LSan binary не сообщает leaks; 307/307
  tests проходят при исключении отдельно зарегистрированного flaky B34.

### B39. Coroutine allocation-failure protocol внутренне противоречив

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файлы и символы:** `include/ace/core/traits/promise.h`
  (`promise_traits::operator new()`), `include/ace/core/async.h`
  (`get_return_object_on_allocation_failure()`, raw-handle constructor).
- **Симптом:** coroutine `operator new(size_t) noexcept` вызывает throwing
  `arena::allocate()`, поэтому allocation failure завершает процесс через
  `std::terminate`. Одновременно объявлен стандартный allocation-failure hook,
  возвращающий `async(nullptr)`, но raw-handle constructor без проверки вызывает
  `_coroutine.promise()` у null handle.
- **Требуемый контракт:** выбрать один согласованный путь: либо allocation
  exception распространяется согласно C++ coroutine protocol, либо non-throwing
  allocation возвращает null и создаёт безопасный пустой `async`. Нельзя
  сохранять одновременно недостижимый fallback и null dereference.
- **Предлагаемое решение:** согласовать exception/noexcept API с arena, затем
  удалить несовместимый hook либо сделать всю null-return цепочку безопасной.
  Учесть переполнение `mem_size + control_block_size` до allocation.
- **Проверка решения:** controlled arena limit/failing allocator без гигантских
  выделений; lazy/eager/automaton paths; наблюдение, move и destruction результата;
  отсутствие terminate, double free и обращения к null promise. Обновить Doxygen
  `@throws` или empty-result contract.

### B40. Move-assignment `async` теряет уже принадлежащую destination корутину

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/async.h` (`async::operator=(async&&)`).
- **Симптом:** assignment без освобождения перезаписывает destination handle и
  обнуляет source. Незавершённый старый frame, router, waiters, backups и
  control-block refs destination становятся недоступными. Self-move также
  обнуляет единственный handle.
- **Пробел теста:** `context_fixture.async_move_leaves_source_null` проверяет
  только move construction; assignment в непустую destination и self-move
  отсутствуют.
- **Предлагаемое решение:** до transfer выполнить тот же согласованный lifecycle,
  что destructor, либо реализовать move-and-swap с корректным release; self-move
  должен быть явным no-op. Не вызывать coroutine callbacks дважды.
- **Проверка решения:** empty/non-empty destination, completed/suspended/observed
  source и destination, backups/insure, waiters, self-move; точные destructor
  counters и arena/control-block accounting под ASan/LSan.

### B41. Move-assignment `io::buffer` теряет destination chunks и ломает self-move

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/io.h` (`io::buffer::operator=(buffer&&)`).
- **Симптом:** assignment перезаписывает `_hdr` и три указателя destination без
  `clear()`, теряя chunks и assembled iovec. При `buffer = std::move(buffer)`
  поля сначала копируются сами в себя, затем обнуляются, поэтому payload
  становится недоступным и не освобождается.
- **Пробел теста:** `io_buffer_fixture.buffer_move_assign` использует пустую
  destination и не проверяет release старого payload, assembled state либо
  self-move; тест проходит на текущем дефекте.
- **Предлагаемое решение:** self-check, затем гарантированно освободить прежнее
  состояние destination и атомарно принять всё состояние source. Уточнить
  exception guarantee; текущая операция заявлена `noexcept`.
- **Проверка решения:** непустая и assembled destination, много chunks, empty
  source, self-move; точные данные/len/msg_iovlen и arena live-chunk deltas;
  ASan/LSan на отсутствие leak/UAF/double-free.

### B42. `io::buffer::shape()` допускает OOB-read и invalidates assembled metadata

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/io.h` (`io::buffer::shape()`).
- **Симптом:** метод документирован как shrink, но не проверяет `len <=` длины
  tail chunk и копирует `len + control_hdr_len` bytes. Больший `len` читает за
  старым chunk. Вызов после `assemble()` заменяет и освобождает tail, оставляя
  `_hdr.msg_iov` со ссылкой на освобождённый payload.
- **Предлагаемое решение:** определить ошибочный input contract (checked return,
  exception либо assert для internal precondition), запретить modification в
  assembled state так же, как `expand/append`, либо безопасно disassemble и
  rebuild metadata. Сохранить `_total_len`, links и arena ownership при failure.
- **Проверка решения:** oversize, equal size, zero, single/multiple chunks,
  before/after assemble и allocation failure; canary/ASan test обязан падать на
  текущей реализации. Обновить Doxygen с preconditions/error semantics.

### B43. Primary template `io::buffer::as<T>()` принимает неподдерживаемые типы

- **Статус:** Открыто.
- **Приоритет:** Средний.
- **Файл:** `include/ace/io.h` (`buffer::as<T>()`).
- **Симптом:** `static_assert("No ...")` всегда истинна, поскольку string literal
  преобразуется в `true`. Любой неподдерживаемый default-constructible `T`
  компилируется и молча возвращает пустое значение вместо диагностики.
- **Предлагаемое решение:** использовать dependent-false assertion либо
  constraint/удалённый primary overload, сохранив только явно поддержанные
  conversions. Текст Doxygen не должен обещать default value как fallback, если
  это не осознанный публичный контракт.
- **Проверка решения:** compile-fail/`requires` checks для unsupported type и
  положительные checks для `std::string` и `std::vector<std::byte>`.

### B44. Byte-size multiplication в buffer/net overloads не проверяет overflow

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файлы и символы:** `include/ace/io.h` (`buffer::emplace` для vector/span),
  `include/ace/net.h` (typed send/recv/sendto overloads).
- **Симптом:** `element_count * sizeof(T)` вычисляется как `size_t` без checked
  multiplication. При wraparound выделяется/передаётся малая длина, после чего
  `memcpy` либо kernel operation использует контракт, не соответствующий
  исходному диапазону. Это отдельная проблема от B25: здесь ошибка возникает до
  сужения на системной границе.
- **Предлагаемое решение:** единый checked byte-count helper либо локальные
  guards до allocation/submission; failure не должен менять buffer/entity.
- **Проверка решения:** synthetic spans/counts у границы `SIZE_MAX / sizeof(T)`
  без реального огромного allocation, типы разных размеров, zero length и все
  overload families. Зафиксировать exception/error contract в Doxygen.

### B45. Move-конструктор intrusive `queue` оставляет nodes привязанными к source

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/tools/queue.h` (`queue(queue&&)`,
  `q_node::owning_queue`, `q_node::remove()`).
- **Симптом:** head/tail переходят в destination, но `owning_queue` каждого узла
  продолжает указывать на moved-from queue. Последующий `node->remove()` изменяет
  пустой source через links destination и может повредить обе очереди.
- **Пробел теста:** `queue_fixture.queue_move_constructor` извлекает элементы
  только через destination и не вызывает self-ejection сохранённого node.
- **Предлагаемое решение:** либо обновить back-pointers всех перенесённых nodes
  (O(N)), либо перепроектировать owner token/indirection так, чтобы move очереди
  сохранял O(1) и `remove()` находил актуального владельца. Выбор важен для
  O(1)-контракта timer cancellation.
- **Проверка решения:** сохранить pointers на head/middle/tail, переместить queue,
  удалить каждый через `q_node::remove()`, проверить links/destructors/reuse;
  repeated moves и empty queue. Документировать complexity move/remove.

### B46. Уничтожение непустой `queue<T>` не разрушает живые `T`

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/tools/queue.h` (`queue`, `slab_mempool`).
- **Симптом:** у `queue` нет destructor, дренирующего nodes. `T` создаётся
  placement-new, но `delete[]` slabs вызывает только destructor `q_node`, который
  не разрушает объект в raw storage. Оставшиеся timer/service payloads пропускают
  destructors и могут удерживать ресурсы.
- **Пробел теста:** `slab_mempool_destructor` возвращает узлы или использует
  payload без наблюдаемого destructor counter.
- **Предлагаемое решение:** определить ownership между queue и pool и обеспечить
  разрушение каждого constructed payload ровно один раз до освобождения slab.
  Учесть, что несколько queue могут разделять один pool.
- **Проверка решения:** tracked non-trivial type, непустые несколько очередей,
  unlink/pop/dequeue/move/destruction permutations; точные construct/destruct
  counts под ASan/LSan.

### B47. `slab_mempool` маскирует allocation failure и затем разыменовывает null

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/tools/queue.h` (`grow()`, `alloc()`, enqueue APIs).
- **Симптом:** `grow()` ловит exception, пишет в `std::cerr` и возвращается;
  `alloc() noexcept` затем без проверки разыменовывает `free_head == nullptr`.
  Если `new[]` успешен, а `slabs.push_back()` бросает, новый slab также теряется.
  Placement-construction `T` вызывается из `noexcept enqueue`, хотя copy/move
  constructor `T` может бросить.
- **Предлагаемое решение:** выбрать явный failure contract и обеспечить strong
  cleanup при каждом участке allocation/construction. Не продолжать после
  failure и не печатать из generic container как замену передаче ошибки.
- **Проверка решения:** failing allocator/injected vector growth и throwing
  payload constructors; первый slab и последующие growth; отсутствие null
  dereference, terminate и leak; очередь остаётся согласованной.

### B48. `dispatcher::worker_state::_pending` образует data race

- **Статус:** Открыто.
- **Приоритет:** Критический.
- **Файл:** `include/ace/core/dispatcher.h` (`worker_state`, `worker_round()`,
  `worker_tf()`, `run()`).
- **Симптом:** worker threads пишут обычный `bool _pending`, а main thread
  одновременно читает его в polling loop без mutex/atomic/happens-before. Это UB;
  `run()` может преждевременно завершиться, бесконечно ждать или читать устаревшее
  состояние.
- **Предлагаемое решение:** определить протокол публикации idle/activity с
  подходящими atomic memory orders либо другим synchronization primitive.
  Простая замена типа недостаточна без доказательства terminal condition при
  concurrent reattach/schedule.
- **Проверка решения:** TSAN, многопоточная подача задач во время `run()`, задачи,
  которые мигрируют/порождают новые задачи на границе idle, тысячи shuffled
  повторов; run возвращается только после доказанной quiescence.

### B49. `reload()` меняет конфигурацию нетранзакционно и принимает ноль runner-ов

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/dispatcher.h` (`fetch_config()`, `reload()`,
  `round_robin()`, `schedule()`, `run()`).
- **Симптом:** `_runners_amount` обновляется до проверки `empty()`. Если reload
  занятого dispatcher возвращает `false`, размер vectors остаётся старым, а
  amount уже новый. Значение 0 очищает vectors; затем single/round-robin paths
  получают OOB или modulo zero, а `run()` резервирует `workers_amount - 1`.
- **Требуемый контракт:** конфигурация либо полностью принята, либо состояние
  dispatcher не меняется. Минимум один runner должен быть валидирован до commit.
- **Предлагаемое решение:** прочитать candidate отдельно, валидировать range и
  idle condition, подготовить новое состояние с exception safety, затем commit.
  Решить, возвращается ли structured error либо `false`.
- **Проверка решения:** zero, one, increase/decrease, same value, reload при
  pending/running work, allocation failure; после отклонения старый dispatcher
  продолжает корректно schedule/run. Обновить config/API Doxygen.

### B50. Полностью переработать weighted scheduler как O(1) load-aware balancer

- **Статус:** Открыто; требуется отдельное архитектурное решение до реализации.
- **Приоритет:** Критический для multi-runner производительности и correctness.
- **Файлы и символы:** `include/ace/core/dispatcher.h` (`schedule()`,
  `_aggregate_velocity`, `_runner_selector`), `include/ace/core/runner.h`
  (`_tasks_amount`, `_quants`, `velocity()`, `upgrade_velocity()`),
  `include/ace/core/tools/moving_average.h`; новые tests/benchmarks.
- **Текущие дефекты:** selection проходит все runners, то есть O(N). Вес
  `1 - velocity/aggregate` не нормализован: сумма привлекательностей равна
  `N - 1`, а random value берётся из `[0,1]`, поэтому ранние элементы получают
  несоразмерную долю, поздние могут почти не выбираться. Статические `mt19937` и
  distribution совместно используются без synchronization. `_tasks_amount` и
  `_quants` читаются scheduler-ом одновременно с записью worker-а без безопасной
  публикации, поэтому сам load signal имеет data races.
- **Цель задачи:** заменить текущий алгоритм целиком. Scheduler обязан выбирать
  runner на основании актуальной наблюдаемой нагрузки runner-ов. Текущий
  `velocity` можно переопределить или заменить: допустимы queue depth, runnable
  work, service/polling weight, throughput/latency EWMA и их O(1)-комбинация.
  Метрика должна быть размерностно осмысленной, устойчива к idle/zero values и
  публиковаться thread-safe без глобального contention.
- **Требование сложности:** hot-path `publish/update load`, `select runner` и
  `schedule/attach metadata` должны стремиться к worst-case либо доказанной
  expected/amortized O(1), независимо от числа runners. Нельзя заменять текущий
  scan на sorting, heap/Fenwick O(log N) или периодический O(N) rebuild в hot
  path. O(N) initialization/reload допустим только как явно вынесенная cold-path
  стоимость O(1) на runner; исполнитель обязан приложить таблицу complexity всех
  операций и обосновать каждое отклонение от O(1).
- **Допустимое направление:** bounded-choice/bucketed либо иная approximate
  weighted схема с константным числом samples и O(1) обновлением. Exact global
  weighted distribution не является самоцелью, если она конфликтует с O(1):
  важнее доказуемо направлять работу от перегруженных runner-ов к менее
  загруженным и не допускать starvation. Конкретный дизайн должен быть утверждён
  отдельно, без добавления новой abstraction заранее.
- **Correctness-тесты:** deterministic injectable randomness/selector;
  одинаковая нагрузка без систематического index bias; один и несколько явно
  перегруженных runner-ов; быстро меняющаяся нагрузка; idle/zero metric; 1/2/4/64+
  runners; concurrent external `schedule()`; отсутствие starvation и bounded
  convergence после смены нагрузки. Тест должен наблюдать фактические runner IDs
  и load, а не только общее число завершённых задач.
- **Benchmarks и критерии:** добавить отдельные сценарии selection-only и
  end-to-end schedule/run для 1, 2, 4, 8, 16, 64+ runners; balanced/skewed/bursty
  load; несколько producer threads. Фиксировать selections/sec, ns/schedule,
  p50/p95/p99 queue wait, throughput, max/mean load imbalance, starvation count
  и scaling относительно N. Сравнение выполнять по N5 в одном окружении и
  доказать отсутствие линейного роста selection cost. Обновить
  `agents/BENCHMARKS.md`, `agents/TESTING.md`, `agents/INDEX.md` и Doxygen.

### B51. `reset_signal()` извлекает не более одного сигнала

- **Статус:** Открыто.
- **Приоритет:** Средний/высокий.
- **Файл:** `include/ace/core/dispatcher.h` (`ace::reset_signal()`).
- **Симптом:** условие `while (not pop(sgl) and not empty())` прекращает цикл при
  успешном первом `pop`; накопившиеся signals остаются в глобальном pipe и могут
  влиять на следующий test/run. Это возможный конкретный источник B14.
- **Пробел тестов:** `interrupt_signal` и `terminate_signal` кладут ровно один
  signal, вызывают reset и завершаются `SUCCEED()` без проверки pipe state.
- **Предлагаемое решение:** дренировать до документированного empty result,
  не инвертируя семантику `pop`; учесть concurrent producers и определить,
  гарантирует ли reset snapshot-drain или quiescent drain.
- **Проверка решения:** смесь нескольких break/shutdown signals, повторный reset,
  empty pipe, следующий `run()` без stale action; repeated/shuffled suite.

### B52. `noexcept` публичных scheduler/container APIs не соответствует операциям

- **Статус:** Открыто; требуется аудит контрактов.
- **Приоритет:** Высокий.
- **Файлы и символы:** `include/ace/core/dispatcher.h` (`reload`, `schedule`,
  `run`), `include/ace/core/traits/service.h` (`respawn`, `touch`),
  `include/ace/core/tools/queue.h` и связанные runner paths.
- **Симптом:** функции помечены `noexcept`, но выполняют `vector::resize/reserve`,
  создают `std::jthread`, конструируют payloads, выделяют queue nodes и вызывают
  scheduling paths с потенциальными исключениями. Failure превращается в
  `std::terminate` вместо заявляемого/обрабатываемого результата.
- **Предлагаемое решение:** составить call graph потенциально throwing операций;
  для каждого публичного API либо снять `noexcept` и документировать `@throws`,
  либо полностью обработать failure с rollback/error result. Не ловить всё с
  продолжением в частично изменённом состоянии.
- **Проверка решения:** injected allocation/thread-construction/payload failures,
  transactional state checks, отсутствие terminate и leak. Добавить compile-time
  `noexcept(...)` assertions для принятого контракта.

### B53. `socket::setup_query()` скрывает отказ submission

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/net.h` (`ace::net::socket::setup_query()`).
- **Симптом:** метод вызывает `kernel_controller::socket(...)`, игнорирует его
  bool-result и всегда возвращает `true`. При отказе controller query сообщает,
  что suspension установлена, хотя CQE не будет; coroutine может зависнуть.
- **Предлагаемое решение:** вернуть фактический submit result и согласовать с
  общим `io::query::await_suspend/await_resume` contract: немедленный отказ
  должен давать определённую ошибку, а не sentinel `INT_MIN` или ложный success.
- **Проверка решения:** принудительно заполненный/rejecting submission buffer и
  failed controller init, отсутствие suspension/hang, корректная invalid socket
  entity/error; успешный socket creation без регрессии.

### B55. Address overloads неверно используют `string_view` и игнорируют `inet_pton`

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/net.h` (string-address overloads `bind`, `connect`,
  `accept` у net entities).
- **Симптом:** `inet_pton(domain_v, addr.data(), ...)` требует NUL-terminated
  string, чего `std::string_view` не гарантирует. Return 0 (invalid text) и -1
  (unsupported family/error) игнорируются; transition продолжает работу с
  нулевым или прежним address storage и потребляет entity.
- **Предлагаемое решение:** обеспечить bounded NUL-terminated representation или
  использовать parser с length; валидировать return и передавать parse failure до
  consuming transition. Согласовать с B23 domain-specific storage.
- **Проверка решения:** substring view с мусором сразу после view, non-terminated
  array, invalid IPv4/IPv6, embedded NUL, valid endpoints; parse failure не
  выполняет kernel operation и соблюдает выбранный B26 ownership contract.

### B56. Public `listener_entity::accept` overload не компилируется при instantiation

- **Статус:** Открыто.
- **Приоритет:** Средний.
- **Файл:** `include/ace/net.h` (`listener_entity::accept(sockaddr*, const
  socklen_t*, int)`, `accept_query` constructor).
- **Симптом:** public overload принимает `const socklen_t*`, но передаёт его
  constructor-у, ожидающему writable `socklen_t*`; kernel accept изменяет длину.
  Template body не диагностируется, пока overload не инстанцирован, поэтому
  текущая сборка проходит.
- **Предлагаемое решение:** публично принимать корректный in/out pointer либо
  изменить API на безопасную owned result structure; не использовать const-cast.
- **Проверка решения:** отдельный consumer compile-test инстанцирует overload;
  runtime loopback проверяет обновлённые address и addrlen, short buffer и error.
  Обновить Doxygen direction `[in,out]`.

### B57. `channel::pending_push()` повторно перемещает один payload в retry-loop

- **Статус:** Открыто; статически подтверждено, runtime зависит от контракта
  внешней queue.
- **Приоритет:** Высокий для move-only payloads.
- **Файл:** `include/ace/futures/channel.h` (оба `pending_push` overload).
- **Симптом:** каждая неуспешная итерация вызывает
  `_container.push(std::forward<data_t>(data))`. Если failed bounded push имеет
  право переместить аргумент до сообщения overflow, следующая попытка отправляет
  moved-from value. Rvalue-reference overload также удерживает reference через
  suspension, что требует явного lifetime contract.
- **Предлагаемое решение:** проверить контракт nukes queue; хранить payload во
  владеющем coroutine frame и перемещать ровно на успешной commit-операции либо
  использовать API, гарантирующий отсутствие consume при failure. Удалить
  дублирующий overload, если он создаёт dangling-reference риск.
- **Проверка решения:** bounded-full channel с move-only tracked payload,
  несколькими suspend/retry, producer cancellation и destruction source после
  вызова; доставляется исходное значение ровно один раз, counters согласованы.

### B58. `service_traits::touch()` и `inspect()` возвращают разные instances

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/traits/service.h` (`touch_impl()`, `inspect()`,
  shared detached accessors).
- **Симптом:** обе функции объявляют собственный function-local static
  `derived_t instance`. В thread-local mode `touch` использует thread-local
  instance, а `inspect` — отдельный process-static; в shared mode также создаются
  два разных объекта. Instance fields и `_shared_detached` читаются/пишутся не у
  того объекта, который выполняет `ping()`.
- **Причина маскировки:** clock/kernel services в основном используют static или
  thread-local данные, поэтому текущие тесты не наблюдают identity/state split;
  отдельного service_traits fixture нет.
- **Предлагаемое решение:** единый storage accessor для каждого spawn mode,
  используемый touch/inspect/detach accessors; сохранить ровно одну instance на
  thread либо process согласно контракту.
- **Проверка решения:** test derived service с нестатическими identity/counters,
  адреса touch/inspect, несколько threads, detach/respawn и destruction order.

### B59. Shared service может быть запущен повторно конкурентными `touch()`

- **Статус:** Открыто.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/core/traits/service.h` (`touch_impl`, `respawn`,
  `_shared_detached`).
- **Симптом:** shared mode выполняет check-then-act: несколько threads читают
  `detached == true`, каждый вызывает `schedule(service(...))`, затем записывает
  false. Один shared instance получает несколько eternal service coroutines.
- **Предлагаемое решение:** атомарно claim-ить право respawn через CAS или
  эквивалентный state machine (`detached/spawning/running/stopping`), публикуя
  state до schedule и откатывая его при failure. Согласовать memory orders и B52.
- **Проверка решения:** barrier-start десятков concurrent touch calls, ровно один
  service loop/runner attachment, повторный spawn только после detach, injected
  scheduling failure, TSAN.

### B60. Type-erased storage `kernel_entity::_params` не проверяет size/alignment

- **Статус:** Открыто; защитный инвариант отсутствует.
- **Приоритет:** Средний.
- **Файл:** `include/ace/services/kernelic.h` (`kernel_entity`, constructor,
  `action_templ`).
- **Симптом:** tuple параметров placement-new размещается в фиксированном
  `uintptr_t _params[8]`, но нет `static_assert(sizeof(tuple) <= sizeof(_params))`
  и проверки alignment. Новый/изменённый io_uring wrapper может тихо записать за
  storage; тип также копируется без явного destruction contract.
- **Предлагаемое решение:** зафиксировать compile-time size/alignment/triviality
  constraints либо заменить storage на корректный type-erasure с destroy/move.
  Не расширять buffer наугад без инвентаризации всех signatures.
- **Проверка решения:** compile-time checks всех текущих submit instantiations,
  deliberately oversized/non-trivial tuple diagnostic, deferred enqueue/move/
  apply/destruction под sanitizers.

### B61. Compose/channel/cutex tests не проверяют заявленные операции

- **Статус:** Открыто; тестовый аудит 2026-08-27.
- **Приоритет:** Высокий.
- **Файлы:** `tests/compose_extra_fixture.cpp`,
  `tests/channel_extra_fixture.cpp`, `tests/cutex_extra_fixture.cpp`.
- **Ложноположительные сценарии:** `operator_pipe` вообще не использует
  `operator>>`; `or_await_left_wins` принимает победу любой ветки при 10 ms против
  2000 ms; `and_await_both_succeed` использует одинаковые timers и marker после
  await не доказывает ожидание обеих веток; `mpsc_channel` имеет одного producer
  и проверяет только сумму; `proxy_double_sync` дважды вызывает `release()`, а не
  `sync()`. Все эти тесты проходят в свежей GCC/ASan-сборке.
- **Предлагаемое решение:** заменить каждый сценарием через фактический публичный
  API и наблюдаемое различающее поведение. Не переименовывать тест под текущую
  слабую проверку, если заявленная возможность должна поддерживаться.
- **Проверка решения:** regressions должны быть mutation-sensitive: удаление
  tested operator/второй branch/одного producer/второго sync обязано приводить к
  failure. Для async ordering использовать barrier/channels, а не только wall
  clock; обновить fixture map и статусы в `agents/TESTING.md`.

### B62. Tests roaming/polling/multi-runner/interrupt не наблюдают семантику

- **Статус:** Открыто; тестовый аудит 2026-08-27.
- **Приоритет:** Высокий.
- **Файлы:** `tests/spawn_extra_fixture.cpp`,
  `tests/cross_mechanic_fixture.cpp`, `tests/dispatcher_fixture.cpp`.
- **Ложноположительные сценарии:** `roaming_true/false` и `polling_true` проверяют
  только marker completion, не flag, routing или service pool;
  `multi_runner_spawn` считает результаты, но не доказывает использование разных
  runner-ов; `interrupt_during_timeout` вызывает interrupt уже после завершения
  `ace::run()`; signal tests завершаются `SUCCEED()` после push/reset.
- **Предлагаемое решение:** публиковать runner identity и наблюдаемые lifecycle
  transitions через public futures/handles; вводить barrier, чтобы interrupt
  происходил во время suspended timeout; проверять содержимое/эффект signals и
  отсутствие stale state. Не читать private fields ради удобства теста, если
  observable public path существует.
- **Проверка решения:** каждый true/false mode даёт различимый результат;
  multi-runner test видит минимум два runner ID; interrupt отменяет/прерывает
  именно активное ожидание; repeated/shuffled прогон не зависит от B14.

### B63. Filesystem и socket echo tests могут пройти без единой успешной операции

- **Статус:** Открыто; тестовый аудит 2026-08-27.
- **Приоритет:** Критический для интеграционного покрытия.
- **Файлы:** `tests/fs_fixture.cpp`, `tests/socket_echo_fixture.cpp`.
- **Ложноположительные сценарии:** fs helpers молча выходят при open/read failure,
  а tests проверяют только `ace::empty()`; записанные bytes и прочитанный content
  не сравниваются. Socket helpers делают `co_return` при любом
  socket/bind/listen/connect/accept failure; send/recv payload только печатается,
  tests снова проверяют empty. ZC client сравнивает положительный byte count с
  `EXIT_SUCCESS == 0`. Фиксированные ports 8000/8001/9000/9001 создают collisions.
  `flexing.txt` и `test_write_read.txt` не очищаются fixture-ом.
- **Предлагаемое решение:** helpers должны всегда публиковать structured outcome
  со stage/error/byte-count/payload; любое неожиданное системное отклонение —
  явный failure, а unsupported environment — явный skip только по заранее
  определённой причине. Использовать unique temporary paths и ephemeral/derived
  ports, cleanup в RAII/TearDown.
- **Проверка решения:** exact file contents/size и exact пять framed messages в
  обе стороны; partial send/recv и errors; тесты демонстративно падают при
  invalid path/port/disabled listener. Нужна граница сообщений: TCP не сохраняет
  send boundaries, поэтому нельзя считать один `recv_buf` одним message без
  framing/EOF protocol.

### B64. Timer tests публикуют requested values вместо фактического времени

- **Статус:** Открыто; тестовый аудит 2026-08-27.
- **Приоритет:** Высокий.
- **Файл:** `tests/timer_fixture.cpp`.
- **Ложноположительные сценарии:** `do_timer_on_runner_test` отправляет в channel
  исходную duration, поэтому tolerance проверяет вход теста; `do_expire_on_runner_test`
  отправляет заданный deadline, а не wake timestamp; `timeout_short` допускает
  elapsed >= 0 для timeout 10 ms. Parallel test создаёт 100000 timers в
  correctness suite, имеет слабую sum-проверку и одновременно служит benchmark.
- **Предлагаемое решение:** измерять `steady_clock` непосредственно вокруг await,
  сопоставлять уникальный timer ID с requested/observed timestamps, отдельно
  проверять нижнюю и разумную верхнюю границы. Для absolute expire проверять, что
  wake не раньше deadline. Масштаб correctness сделать детерминированным и
  умеренным; тяжёлую нагрузку оставить benchmark-у.
- **Проверка решения:** zero/negative/sub-ms/boundary wheel slots, concurrent
  timers, cancel, multi-runner; статистически устойчивые допуски без принятия
  мгновенного completion. Много повторов/shuffle по B34.

### B65. Ownership/console/outcast tests используют `SUCCEED` вместо наблюдаемых инвариантов

- **Статус:** Открыто; тестовый аудит 2026-08-27.
- **Приоритет:** Высокий.
- **Файлы:** `tests/io_any_fixture.cpp`, `tests/console_fixture.cpp`,
  `tests/io_hanged_fixture.cpp`, `tests/context_fixture.cpp`,
  `tests/promise_traits_fixture.cpp`, `tests/queue_fixture.cpp`.
- **Ложноположительные сценарии:** все `io_any` lifecycle tests не считают
  move/destruction/deleter; console tests проверяют только отсутствие exception,
  не bytes/newline/format; outcast capture tests проходят даже при `capture ==
  false`, а `hanged_command_defaults` вопреки `agents/TESTING.md` намеренно не
  проверяет defaults; `kernel_register_files` выполняет assertions только
  внутри успешной ветки
  `pipe()` и молча проходит при failure; layout/buffer/queue gaps описаны в
  B41/B45/B46. Layout gap B13 закрыт exact-size и canary regressions.
- **Предлагаемое решение:** tracked payloads/canaries/counters, deterministic
  stdout sink/capture без реального kernel console, обязательный ASSERT на
  resource acquisition, точные command reset/retain semantics. Разделить
  standalone runner tests от console/io_uring, чтобы B38 не скрывал их смысл.
- **Проверка решения:** tests должны падать при пропуске deleter, double move,
  stale command field, неверном newline и нулевом frame-size metadata; включить
  LSan-capable job либо эквивалентное arena/pool accounting.

### B66. Sanitizer/test-discovery configuration не даёт надёжной общей проверки

- **Статус:** Решено 2026-08-27.
- **Приоритет:** Высокий.
- **Файлы:** `meson.build`, `discover_tests.py`, `agents/TESTING.md`.
- **Симптом:** test target всегда принудительно включает только ASan и задаёт
  `detect_leaks=0` для discovered GTests, поэтому queue/buffer ownership leaks не
  обнаруживаются. UBSan/TSan jobs отсутствуют. `ace_tests.discovery_consistency`
  не получает это env и в свежих GCC/Clang build-dir падает до сравнения списка:
  LeakSanitizer не может работать в текущем ptrace environment. При ручном
  `ASAN_OPTIONS=detect_leaks=0` verify проходит; актуально он подтверждает 304
  GTest, а Meson регистрирует 306 тестов. При одновременно включённых tests+benchmarks могут
  дополнительно регистрироваться tests fallback-проекта Google Benchmark, что
  искажает ACE count.
- **Решение:** `test_sanitizers` создаёт отдельные ASan, ASan+UBSan и TSan
  profiles, не позволяя смешать TSan с другими runtimes. Единый
  `tests/sanitized_test_runner.py` оборачивает GTests, discovery и entry helper,
  задаёт strict sanitizer options и probe-ит LSan. `enabled` requires leak-capable
  environment; `auto` отдаёт Meson SKIP (77) для отдельного capability test
  under ptrace, но продолжает correctness checks с `detect_leaks=0` только там.
  ACE registrations имеют suite `ace`; fallback Google Benchmark получает
  `tests=disabled`.
- **Проверка:** Clang 22 ASan: discovery OK и LSan capability SKIP under ptrace.
  GCC 16 ASan+UBSan и TSan clean directories: targeted B29/B68, launcher и
  discovery 6/6. Full suite remains environment-limited only by successful I/O.

### B67. Пользовательская и агентская документация противоречит текущему коду

- **Статус:** Открыто.
- **Приоритет:** Средний.
- **Файлы:** `README.md`, `agents/INDEX.md`, `agents/TESTING.md`,
  `include/ace/services/clock.h`.
- **Расхождения:** README называет `automaton<T>` eager и diagram запускает его
  вызовом, тогда как implementation/INDEX/test фиксируют lazy
  `suspend_always`; test inventory и compiler status в INDEX синхронизированы
  при решении B13, но clock Doxygen всё ещё обещает refresh `cached_now()` при
  возрасте >1 ms, но код и решённый B6 задают только каждый 16-й вызов;
  `agents/TESTING.md` приписывает `hanged_command_defaults` проверки, которых в
  test body нет.
- **Предлагаемое решение:** после исправления соответствующих production/test
  issues выбрать authoritative contracts и синхронно обновить все четыре
  документа. Не менять implementation для совпадения с устаревшим текстом без
  отдельного API-решения.
- **Проверка решения:** ручная cross-reference проверка coroutine table/diagram,
  test counts и compiler status; Doxygen соответствует B6; fixture map точно
  описывает assertions. Добавить lightweight doc/count consistency checks там,
  где это не требует дублировать данные.

### B68. Nukes freelists размещают over-aligned nodes в обычном `malloc` storage

- **Статус:** Решено локальным vendored patch 2026-08-27.
- **Приоритет:** Критический.
- **Файлы и граница ACE:**
  `subprojects/nukes/include/nukes/dynamic/{mpmc,spmc,regular}_freelist.h`,
  `nukes/details/node_types.h`, `nukes/details/constants.h`; ACE инстанцирует эти
  контейнеры в `include/ace/futures/channel.h`, runner/signal/arena/id allocator.
- **Симптом:** `dyn_node<T>::_data` получает alignment
  `bit_ceil(sizeof(T))`, из-за чего вложенный node ACE достигает alignment 128.
  Freelist выделяет его через `malloc(sizeof(node_t))`, который гарантирует лишь
  fundamental/max alignment, и выполняет placement-new по потенциально
  misaligned адресу. Комбинированный GCC ASan+UBSan остановился до GTest на
  `mpmc_freelist::capture()` для глобального `ace::bus<int>` с сообщением
  `store to misaligned address ... requires 128 byte alignment`.
- **Решение:** `word_alignment<T>` derives from real `alignof(T)` rather than
  `bit_ceil(sizeof(T))`. New internal `node_allocation.h` uses matched aligned
  new/delete above `max_align_t`; all MPMC/SPMC/regular initial and growth
  allocations and teardown paths use it. Teardown preserves the existing
  freelist protocol in which `sync()` has already destroyed a returned payload.
- **Регрессии и проверка:** `nukes_alignment_fixture` validates recovered node
  and payload addresses for alignments 1/8/16/32/64/128/256 across all three
  freelists and capture/sync/capture. Clang 22 ASan target repeats pass; clean
  GCC 16 ASan+UBSan and TSan targeted suites pass without B68 diagnostics.

### B69. `mpmc_freelist` move operations leave source ownership intact

- **Статус:** Открыто; обнаружено при B68 move-path audit 2026-08-27.
- **Приоритет:** Высокий.
- **Файл:** `subprojects/nukes/include/nukes/dynamic/mpmc_freelist.h`.
- **Симптом:** move constructor and move assignment copy `_head`, `_tail` and
  `_dummy_ptr` but do not clear the source. Destruction of both objects can
  release the same node chain twice.
- **Требуемое решение:** transfer ownership transactionally, release an existing
  destination chain in assignment, and null all source pointers. Add a focused
  ASan regression for nonempty move construction/assignment.
- **Проверка решения:** repeated move construction and assignment of nonempty
  MPMC freelists under ASan/UBSan and TSan; no double free/leak and source empty.

### B70. Завершённый outcast command удерживал payload после возврата в pool

- **Статус:** Решено 2026-08-27.
- **Приоритет:** Высокий.
- **Файл:** `include/ace/io.h` (`io::outcast::command::on_result()`).
- **Симптом:** successful fire-and-forget writes возвращали command через
  `raw_sync()` с живым `io::buffer`. Следующий move-assignment перетирал pointers
  старого payload, а последний cached payload переживал teardown без destructor;
  полный LSan-прогон показывал многочисленные leaks из console/timer output.
- **Решение:** completion очищает buffer до `raw_sync()`, сохраняя intended
  lifetime самого command и освобождая принадлежащие buffer arena chunks.
- **Регресс-тест:**
  `io_entity_fixture.outcast_command_completion_releases_payload_before_pool_return`;
  полный host ASan+LSan-прогон не сообщает leaks, Meson suite проходит 312/312.
- **Связь с B37:** исправлен successful completion lifecycle. Reject/fallback
  ownership после успешной инициализации ring остаётся отдельной открытой B37.

### B71. Kernel availability preflight запускал thread-local service на чужом runner

- **Статус:** Решено 2026-08-27.
- **Приоритет:** Критический.
- **Файлы:** `include/ace/services/kernelic.h`
  (`kernel_controller::available()`, `initialization_error()`),
  `tests/io_entity_fixture.cpp`.
- **Симптом:** B38 preflight вызывал `touch(nullptr)`. После migration запрос
  создавался на worker thread, но round-robin мог поставить polling service на
  другой runner/thread с другим thread-local ring. CQE не обрабатывался,
  coroutine и последующий close оставались suspended; LSan показывал два task
  node leaks.
- **Решение:** status methods передают в `touch()` текущий runner pool. Service
  опрашивает тот же thread-local ring, на котором выполняется preflight/submit.
- **Регресс-тест:**
  `io_entity_fixture.connection_link_read_preserves_runner_after_migration`
  проходит 20/20 host repeats; весь `io_entity_fixture` проходит 28/28 одним
  ASan+LSan-процессом без leaks; Meson suite проходит 312/312.

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

### N6. Конвертация и clone `io::buffer` выполняют лишние allocation/move

- **Статус:** Открыто; оптимизация допустима только после B41-B44.
- **Приоритет:** Средний.
- **Файл:** `include/ace/io.h` (`buffer::as<std::string>()`,
  `as<std::vector<std::byte>>()`, `clone()`).
- **Наблюдение:** обе conversions знают `_total_len`, но не делают `reserve`.
  Byte-vector строится по одному `push_back` на byte вместо range copy каждого
  chunk. `clone()` возвращает `std::forward<buffer>(cl)`, что принудительно
  превращает local в xvalue и мешает гарантированному NRVO-style return path.
- **Предлагаемое решение:** после correctness fixes зарезервировать точный итоговый
  размер, копировать contiguous chunk ranges и возвращать named local обычным
  `return cl`. Проверить, можно ли безопасно дать caller-allocated/range API,
  только если текущих overloads недостаточно; новую abstraction заранее не
  добавлять.
- **Correctness-проверка:** empty/single/many chunks, binary NUL bytes,
  prepend/shape/assembled source, exact equality и независимость clone.
- **Benchmark:** добавить отдельные `as_string`, `as_bytes`, `clone` сценарии для
  64 B, 4 KiB, 1 MiB и разного числа chunks при одинаковом total size. Фиксировать
  bytes/sec, allocations, copied bytes и peak memory; несколько прогонов по N5.
  Приёмка — отсутствие новых allocations сверх необходимой destination storage
  и отсутствие regression на single chunk.

### N7. Channel backpressure и cancellation создают polling/thundering herd

- **Статус:** Открыто; сначала исправить B57 и подтвердить семантику.
- **Приоритет:** Средний.
- **Файл:** `include/ace/futures/channel.h`; benchmark BM15.
- **Наблюдение:** full `pending_push` кооперативно повторяет push после `suspend`,
  не регистрируя отдельного producer waiter, поэтому latency/CPU зависят от
  общего polling scheduler. `channel_router::cancel()` дренирует и reattach-ит
  всех consumers, поскольку waiter nodes не имеют адресного cancel/ejection;
  одиночная отмена создаёт O(N) wakeups и cross-runner traffic.
- **Предлагаемое направление:** после фиксации ownership payload оценить отдельные
  producer/consumer waiter queues с адресной O(1) регистрацией/ejection и
  wake-one по освобождению slot. Lifetime node и cancel race должны быть доказаны
  до оптимизации; не переносить проблему в busy spin.
- **Correctness-проверка:** bounded MPSC/MPMC, FIFO согласно выбранному контракту,
  cancel одного waiter не будит/не теряет остальных, close/destruction, runner
  migration и exact-once delivery.
- **Benchmark:** расширить BM15 размерами capacity 1/16/1024, 1..N producers и
  consumers, cancel storms. Фиксировать throughput, suspend/resume и reattach
  counts, CPU time, p95/p99 wait и масштабирование N; сравнивать по N5.

### N8. `dispatcher::run()` пересоздаёт threads и использует фиксированный polling cadence

- **Статус:** Открыто; исследовать после B48-B50 и B52.
- **Приоритет:** Средний.
- **Файл:** `include/ace/core/dispatcher.h` (`run()`, `worker_tf()`,
  `worker_round()`).
- **Наблюдение:** каждый `run()` и каждая внешняя do/while-итерация создаёт новый
  vector `std::jthread`, запускает N-1 threads и уничтожает их после polling
  convergence. Worker busy-poll-ит до 1 ms, затем использует фиксированные 1 us
  или 1 ms sleeps. Для коротких повторных workloads thread lifecycle может
  доминировать, а sleeps задают latency floor.
- **Предлагаемое направление:** сначала измерить доли thread startup, useful work,
  spin и sleep. Рассмотреть persistent workers/condition or atomic wait/adaptive
  backoff только с явным dispatcher lifetime/shutdown/reload contract. Нельзя
  сохранять workers дольше owner arena/storage (критическое ограничение проекта).
- **Benchmark:** repeated 0/1/10/100 short tasks при 1/2/4/8/16 runners, burst-idle
  циклы и polling services. Фиксировать wall/CPU time, thread creations,
  wake-to-run p50/p99, idle CPU и throughput. Baseline/current — в одинаковом
  окружении по N5; оптимизация не должна ухудшать timer/cancellation latency или
  завершение runner threads перед owner storage.

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

### B25. I/O lengths сужались из `size_t` в `unsigned`

- **Статус:** Решено.
- **Файлы:** `include/ace/io.h`, `include/ace/net.h`,
  `include/ace/services/kernelic.h`.
- **Причина:** read/write queries и kernelic wrappers принимали `unsigned`, а
  liburing также неявно приводил публичные `size_t` send/recv lengths к полю
  SQE типа `__u32`.
- **Решение:** API query и kernelic wrappers используют `size_t`; единый предел
  `kernel_controller::max_io_length == UINT_MAX` проверяется до liburing.
  Oversize query завершает normal await-path с `-EOVERFLOW`, не создавая SQE;
  direct kernelic wrapper возвращает `false`.
- **Тесты:** `io_entity_fixture.io_query_lengths_preserve_uint_max_boundary`,
  `oversize_io_queries_return_eoverflow_without_submission` и
  `kernelic_rejects_oversize_lengths_without_submission`.

### B54. `connection_link` блокировал runner системным `recv()`

- **Статус:** Решено.
- **Файл:** `include/ace/net.h` (`connection_link::input_action()`).
- **Причина:** high-level link не мог видеть nested `transport_entity::recv_query`
  и выполнял `::recv()` на runner thread.
- **Решение:** общий namespace-level `net::recv_query` используется и transport,
  и `connection_link`; nested spelling сохраняется через type alias. Read теперь
  проходит через io_uring query-router и поддерживает cancellation.
- **Тесты:** `io_entity_fixture.connection_link_stalled_read_keeps_runner_responsive_and_cancels`,
  `connection_link_read_preserves_partial_eof_and_error_results` и
  `connection_link_read_preserves_runner_after_migration`.
- **Benchmark:** `bm_connection_link_idle_cancel` измеряет cancellation idle
  connections при нагрузках 1, 10 и 100.

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
