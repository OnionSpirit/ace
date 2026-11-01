---
id: "testing:CN2"
source_id: "CN2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 872
---

# CN2. `println_string_view`

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
- **Пункт:** `CN2` — `println_string_view`.
- **Критерий:** println(string_view) через `ace::println`.
- **Статус в архивном плане:** ✅.
