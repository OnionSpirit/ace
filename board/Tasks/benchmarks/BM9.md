---
id: "benchmark:BM9"
source_id: "BM9"
kind: "benchmark"
status: "Решено"
source_status: "Реализованная benchmark-сценарий"
priority: "Не указан"
source_document: "agents/BENCHMARKS.md"
source_line: 78
---

# BM9. bm_timeout_short

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Сценарий:** `bm_timeout_short`.
- **Что измеряет:** Пропускная способность clock: 20k таймеров по 1 ms..
- **Статус в архивном инвентаре:** реализованная benchmark-сценарий.
