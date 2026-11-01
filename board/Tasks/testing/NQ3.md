---
id: "testing:NQ3"
source_id: "NQ3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 758
---

# NQ3. `mpmc_prefilled_fifo`

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
- **Пункт:** `NQ3` — `mpmc_prefilled_fifo`.
- **Критерий:** Строгий FIFO prefilled MPMC.
- **Статус в архивном плане:** ✅.
