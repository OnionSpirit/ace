---
id: "testing:IH2"
source_id: "IH2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 608
---

# IH2. `hanged_fail_handler_positive`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_hanged_fixture`.
- **Пункт:** `IH2` — `hanged_fail_handler_positive`.
- **Критерий:** Прямой basic_fail_handler бросает и для nonnegative result; условие вызова принадлежит command::on_result.
- **Статус в архивном плане:** ✅.
