---
id: testing:FS9
source_id: FS9
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 847
rank: 100352
---

# FS9. `file_output_action`

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
- **Пункт:** `FS9` — `file_output_action`.
- **Критерий:** output_action использует kernel_controller::writev.
- **Статус в архивном плане:** ⬜.
