---
id: "testing:CH6"
source_id: "CH6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 739
---

# CH6. `channel_empty`

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
- **Пункт:** `CH6` — `channel_empty`.
- **Критерий:** empty() на пустом/непустом.
- **Статус в архивном плане:** ✅.
