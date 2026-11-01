---
id: "testing:CI2"
source_id: "CI2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 724
---

# CI2. `partial_initialization_failure_rethrows_and_retries`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.14 futures/timeout.h.
- **Fixture:** `clock_initialization_fixture`.
- **Пункт:** `CI2` — `partial_initialization_failure_rethrows_and_retries`.
- **Критерий:** Частично созданный wheel уничтожается; последующий timeout строит полное состояние.
- **Статус в архивном плане:** ✅.
