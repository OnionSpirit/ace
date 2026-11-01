---
id: "benchmark:BM1"
source_id: "BM1"
kind: "benchmark"
status: "Решено"
source_status: "Реализованная benchmark-сценарий"
priority: "Не указан"
source_document: "agents/BENCHMARKS.md"
source_line: 70
---

# BM1. bm_cutex_race_capture

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Сценарий:** `bm_cutex_race_capture`.
- **Что измеряет:** Пропускная способность cutex capture/release, 8 runner-ов x 100k..
- **Статус в архивном инвентаре:** реализованная benchmark-сценарий.
