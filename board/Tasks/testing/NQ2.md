---
id: "testing:NQ2"
source_id: "NQ2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 757
---

# NQ2. `mpmc_multi_producer_consumer_exact_once`

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
- **Пункт:** `NQ2` — `mpmc_multi_producer_consumer_exact_once`.
- **Критерий:** 4P/4C, 80k значений без loss/duplication.
- **Статус в архивном плане:** ✅.
