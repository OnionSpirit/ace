---
id: "testing:NQ1"
source_id: "NQ1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 756
---

# NQ1. `mpsc_multi_producer_fifo_and_exact_once`

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
- **Пункт:** `NQ1` — `mpsc_multi_producer_fifo_and_exact_once`.
- **Критерий:** 4P/1C, 80k значений, exact-once и per-producer FIFO.
- **Статус в архивном плане:** ✅.
