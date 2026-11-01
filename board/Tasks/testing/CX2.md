---
id: testing:CX2
source_id: CX2
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 771
rank: 83968
---

# CX2. `cutex_try_lock_busy`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.16 futures/cutex.h.
- **Fixture:** `cutex_extra_fixture` (расширение).
- **Пункт:** `CX2` — `cutex_try_lock_busy`.
- **Критерий:** try_lock() на занятом → false.
- **Статус в архивном плане:** ⬜.
