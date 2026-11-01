---
id: "testing:AH9"
source_id: "AH9"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 540
---

# AH9. `check_valued_spawn_cancel`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.9 core/async_handle.h.
- **Fixture:** `spawn_extra_fixture` (расширение spawn-тестов).
- **Пункт:** `AH9` — `check_valued_spawn_cancel`.
- **Критерий:** join() на отменённой valued-таске → nullopt (cancel не даёт статусу стать e_finished).
- **Статус в архивном плане:** ✅.
