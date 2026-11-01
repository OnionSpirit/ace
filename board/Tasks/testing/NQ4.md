---
id: "testing:NQ4"
source_id: "NQ4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 759
---

# NQ4. `mpsc_node_api_reuses_live_storage`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.15 futures/channel.h.
- **Fixture:** `nukes_concurrency_fixture`.
- **Пункт:** `NQ4` — `mpsc_node_api_reuses_live_storage`.
- **Критерий:** Внешний node ownership и восстановленный payload lifetime.
- **Статус в архивном плане:** ✅.
