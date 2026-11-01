---
id: testing:AH4
source_id: AH4
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 535
rank: 44032
---

# AH4. `join_handler_await_resume`

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
- **Пункт:** `AH4` — `join_handler_await_resume`.
- **Критерий:** await_resume возвращает finished для успешной void-корутины (регрессия B4).
- **Статус в архивном плане:** ⬜.
