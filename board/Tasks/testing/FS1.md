---
id: testing:FS1
source_id: FS1
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 839
rank: 95232
---

# FS1. `file_open_rdonly`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.19 fs.h.
- **Fixture:** `fs_fixture`.
- **Пункт:** `FS1` — `file_open_rdonly`.
- **Критерий:** open_rdonly() → file_link.
- **Статус в архивном плане:** ⬜.
