---
id: "testing:CN4"
source_id: "CN4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 874
---

# CN4. `print_format`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.20 console.h.
- **Fixture:** `console_fixture`.
- **Пункт:** `CN4` — `print_format`.
- **Критерий:** print(fmt, args...) без newline через `ace::print`.
- **Статус в архивном плане:** ✅.
