---
id: "benchmark:BM2"
source_id: "BM2"
kind: "benchmark"
status: "Решено"
source_status: "Реализованная benchmark-сценарий"
priority: "Не указан"
source_document: "agents/BENCHMARKS.md"
source_line: 71
---

# BM2. bm_cutex_race_sync

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Сценарий:** `bm_cutex_race_sync`.
- **Что измеряет:** Cutex с rescheduling и миграцией waiter-ов..
- **Статус в архивном инвентаре:** реализованная benchmark-сценарий.
