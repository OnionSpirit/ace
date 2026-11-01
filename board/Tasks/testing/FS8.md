---
id: testing:FS8
source_id: FS8
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 846
rank: 99328
---

# FS8. `file_read_str`

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
- **Пункт:** `FS8` — `file_read_str`.
- **Критерий:** read_str() через file_link.
- **Статус в архивном плане:** ⬜.
