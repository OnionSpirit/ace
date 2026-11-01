---
id: "testing:CH1"
source_id: "CH1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 734
---

# CH1. `push_pull_single`

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
- **Пункт:** `CH1` — `push_pull_single`.
- **Критерий:** push → pull возвращает то же значение.
- **Статус в архивном плане:** ✅.
