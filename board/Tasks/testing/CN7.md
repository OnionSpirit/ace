---
id: testing:CN7
source_id: CN7
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 877
rank: 73728
---

# CN7. `stdout_link`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.20 console.h.
- **Fixture:** `console_fixture`.
- **Пункт:** `CN7` — `stdout_link`.
- **Критерий:** stdout_link() возвращает валидный link.
- **Статус в архивном плане:** ⬜.
