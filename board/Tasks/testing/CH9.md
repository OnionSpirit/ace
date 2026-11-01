---
id: "testing:CH9"
source_id: "CH9"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 742
---

# CH9. `channel_mpmc`

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
- **Пункт:** `CH9` — `channel_mpmc`.
- **Критерий:** 4 producer-а и 4 consumer-а: exact-once delivery и отсутствие stale waiters.
- **Статус в архивном плане:** ✅.
