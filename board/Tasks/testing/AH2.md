---
id: testing:AH2
source_id: AH2
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 533
rank: 41984
---

# AH2. `join_handler_await_ready_done`

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
- **Пункт:** `AH2` — `join_handler_await_ready_done`.
- **Критерий:** await_ready = true когда done.
- **Статус в архивном плане:** ⬜.
