---
id: "testing:FS6"
source_id: "FS6"
kind: "coverage"
status: "Решено"
source_status: "✅ (в file_write_and_read)"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 844
---

# FS6. `file_writeln`

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
- **Пункт:** `FS6` — `file_writeln`.
- **Критерий:** writeln() через file_link.
- **Статус в архивном плане:** ✅ (в file_write_and_read).
