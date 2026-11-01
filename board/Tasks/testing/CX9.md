---
id: testing:CX9
source_id: CX9
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 778
rank: 88064
---

# CX9. `cutex_notify_rescheduling`

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
- **Пункт:** `CX9` — `cutex_notify_rescheduling`.
- **Критерий:** _rescheduling = true → waiter мигрирует.
- **Статус в архивном плане:** ⬜.
