---
id: testing:AY7
source_id: AY7
kind: coverage
status: Открыто
source_status: ⬜ (удалён)
priority: Не указан
source_document: agents/TESTING.md
source_line: 601
rank: 46080
---

# AY7. `any_copy`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_any_fixture`.
- **Пункт:** `AY7` — `any_copy`.
- **Критерий:** Копирование запрещено (shallow-copy → double-free).
- **Статус в архивном плане:** ⬜ (удалён).
