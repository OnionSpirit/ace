---
id: "testing:CH8"
source_id: "CH8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 741
---

# CH8. `mpsc_channel`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.15 futures/channel.h.
- **Fixture:** `channel_extra_fixture` (расширение).
- **Пункт:** `CH8` — `mpsc_channel`.
- **Критерий:** Несколько producer-ов, один consumer.
- **Статус в архивном плане:** ✅.
