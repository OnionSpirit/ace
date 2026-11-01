---
id: testing:AH3
source_id: AH3
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 534
rank: 43008
---

# AH3. `join_handler_await_suspend`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.9 core/async_handle.h.
- **Fixture:** `spawn_extra_fixture` (расширение spawn-тестов).
- **Пункт:** `AH3` — `join_handler_await_suspend`.
- **Критерий:** await_suspend регистрирует waiter.
- **Статус в архивном плане:** ⬜.
