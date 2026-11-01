---
id: testing:AH1
source_id: AH1
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 532
rank: 40960
---

# AH1. `join_handler_await_ready`

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
- **Пункт:** `AH1` — `join_handler_await_ready`.
- **Критерий:** await_ready = false пока не done.
- **Статус в архивном плане:** ⬜.
