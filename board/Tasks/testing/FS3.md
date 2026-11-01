---
id: testing:FS3
source_id: FS3
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 841
rank: 98304
---

# FS3. `file_open_rewrite`

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
- **Пункт:** `FS3` — `file_open_rewrite`.
- **Критерий:** open_rewrite() → file_link.
- **Статус в архивном плане:** ⬜.
