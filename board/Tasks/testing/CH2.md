---
id: testing:CH2
source_id: CH2
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 735
rank: 67584
---

# CH2. `push_full`

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
- **Пункт:** `CH2` — `push_full`.
- **Критерий:** push на полный канал → false.
- **Статус в архивном плане:** ⬜.
