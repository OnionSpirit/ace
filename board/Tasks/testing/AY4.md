---
id: "testing:AY4"
source_id: "AY4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 598
---

# AY4. `any_release`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_any_fixture`.
- **Пункт:** `AY4` — `any_release`.
- **Критерий:** release() освобождает память через deleter.
- **Статус в архивном плане:** ✅.
