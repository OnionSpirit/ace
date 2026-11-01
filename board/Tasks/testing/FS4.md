---
id: "testing:FS4"
source_id: "FS4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 842
---

# FS4. `file_open_fail`

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
- **Пункт:** `FS4` — `file_open_fail`.
- **Критерий:** open() несуществующего файла на чтение → error.
- **Статус в архивном плане:** ✅.
