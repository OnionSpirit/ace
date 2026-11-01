---
id: testing:CX4
source_id: CX4
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 773
rank: 86016
---

# CX4. `cutex_sync_wakes`

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
- **Пункт:** `CX4` — `cutex_sync_wakes`.
- **Критерий:** sync() пробуждает следующего ожидающего.
- **Статус в архивном плане:** ⬜.
