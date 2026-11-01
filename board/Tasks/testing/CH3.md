---
id: testing:CH3
source_id: CH3
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 736
rank: 68608
---

# CH3. `pending_push`

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
- **Пункт:** `CH3` — `pending_push`.
- **Критерий:** pending_push ждёт освобождения.
- **Статус в архивном плане:** ⬜.
