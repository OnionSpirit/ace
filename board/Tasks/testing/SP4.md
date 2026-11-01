---
id: testing:SP4
source_id: SP4
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 804
rank: 123904
---

# SP4. `reattach_changes_runner`

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
- **Пункт:** `SP4` — `reattach_changes_runner`.
- **Критерий:** reattach(runner*) → _runner обновляется.
- **Статус в архивном плане:** ⬜.
