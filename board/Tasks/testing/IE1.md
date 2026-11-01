---
id: "testing:IE1"
source_id: "IE1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 617
---

# IE1. `entity_default_construction`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_entity_fixture`.
- **Пункт:** `IE1` — `entity_default_construction`.
- **Критерий:** entity по умолчанию: _fd = -1, _is_closed = true.
- **Статус в архивном плане:** ✅.
