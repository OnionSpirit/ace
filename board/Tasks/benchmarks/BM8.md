---
id: "benchmark:BM8"
source_id: "BM8"
kind: "benchmark"
status: "Решено"
source_status: "Реализованная benchmark-сценарий"
priority: "Не указан"
source_document: "agents/BENCHMARKS.md"
source_line: 77
---

# BM8. bm_spawn_join

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Сценарий:** `bm_spawn_join`.
- **Что измеряет:** Задержка цикла spawn + join..
- **Статус в архивном инвентаре:** реализованная benchmark-сценарий.

## Контроль после B87 — 2026-11-01

- `bm_spawn_join` median CPU: 2.292198 → 2.390378ms (+4.28%).

ABBA ×5 repeats,10 samples каждой версии, без benchmark errors. GCC16 release,
тот же host и sources; полный protocol, нагрузки, ограничения и сырые paths
в `agents/BENCHMARKS.md`, раздел «B87: стоимость выравнивания frame».

## Compact allocator — 2026-11-08

ABBA×5, GCC16 release, все samples без errors. Padded →compact CPU medians:

| Сценарий | Padded ms | Compact ms | Изменение |
|---|---:|---:|---:|
| `bm_spawn_join` | 2.420347 | 2.289945 | -5.39% |

Полный protocol, ограничения и paths: agents/BENCHMARKS.md, раздел B87 от08.11.
