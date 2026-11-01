---
id: testing:CH7
source_id: CH7
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 740
rank: 70656
---

# CH7. `pull_suspends`

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
- **Пункт:** `CH7` — `pull_suspends`.
- **Критерий:** pull на пустом канале → корутина суспендится.
- **Статус в архивном плане:** ⬜.
