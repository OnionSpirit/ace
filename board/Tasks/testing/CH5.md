---
id: "testing:CH5"
source_id: "CH5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 738
---

# CH5. `operator_left_shift`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.15 futures/channel.h.
- **Fixture:** `channel_extra_fixture` (расширение).
- **Пункт:** `CH5` — `operator_left_shift`.
- **Критерий:** ch << val → push.
- **Статус в архивном плане:** ✅.
