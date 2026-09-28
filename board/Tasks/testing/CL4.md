---
id: "testing:CL4"
source_id: "CL4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 692
---

# CL4. `expire_past_deadline_beats_new_relative_timeout`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.13 services/clock.h.
- **Fixture:** `ClockFixture` (не реализована), частично покрыто через `timer_fixture`.
- **Пункт:** `CL4` — `expire_past_deadline_beats_new_relative_timeout`.
- **Критерий:** Absolute deadline сохраняется до routing.
- **Статус в архивном плане:** ✅.
