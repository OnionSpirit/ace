---
id: testing:FS2
source_id: FS2
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 840
rank: 97280
---

# FS2. `file_open_wronly`

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
- **Пункт:** `FS2` — `file_open_wronly`.
- **Критерий:** open_wronly() → file_link.
- **Статус в архивном плане:** ⬜.
