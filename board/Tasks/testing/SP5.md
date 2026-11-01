---
id: testing:SP5
source_id: SP5
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 805
rank: 124928
---

# SP5. `reattach_router_redirect`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.17 Управление задачами.
- **Fixture:** `spawn_extra_fixture` (расширение).
- **Пункт:** `SP5` — `reattach_router_redirect`.
- **Критерий:** reattach_router::redirect обновляет _runner + reattach.
- **Статус в архивном плане:** ⬜.
