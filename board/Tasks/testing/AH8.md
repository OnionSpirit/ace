---
id: testing:AH8
source_id: AH8
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 539
rank: 45056
---

# AH8. `handle_cancel`

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
- **Пункт:** `AH8` — `handle_cancel`.
- **Критерий:** cancel() отменяет корутину.
- **Статус в архивном плане:** ⬜.
