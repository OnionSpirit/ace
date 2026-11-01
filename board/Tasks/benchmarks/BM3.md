---
id: "benchmark:BM3"
source_id: "BM3"
kind: "benchmark"
status: "Решено"
source_status: "Реализованная benchmark-сценарий"
priority: "Не указан"
source_document: "agents/BENCHMARKS.md"
source_line: 72
---

# BM3. bm_timer_parallel

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Сценарий:** `bm_timer_parallel`.
- **Что измеряет:** Масштабируемость clock: 100k таймеров на 4 runner-ах..
- **Статус в архивном инвентаре:** реализованная benchmark-сценарий.
