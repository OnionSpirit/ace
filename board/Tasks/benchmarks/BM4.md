---
id: "benchmark:BM4"
source_id: "BM4"
kind: "benchmark"
status: "Решено"
source_status: "Реализованная benchmark-сценарий"
priority: "Не указан"
source_document: "agents/BENCHMARKS.md"
source_line: 73
---

# BM4. bm_spawn_cancel

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Сценарий:** `bm_spawn_cancel`.
- **Что измеряет:** Массовый spawn и немедленный cancel..
- **Статус в архивном инвентаре:** реализованная benchmark-сценарий.

## Контроль после B87 — 2026-11-01

- `bm_spawn_cancel` median CPU: 0.006057 → 0.006451ms (+6.50%).

ABBA ×5 repeats,10 samples каждой версии, без benchmark errors. GCC16 release,
тот же host и sources; полный protocol, нагрузки, ограничения и сырые paths
в `agents/BENCHMARKS.md`, раздел «B87: стоимость выравнивания frame».
