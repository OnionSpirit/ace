---
id: "testing:CL2"
source_id: "CL2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 690
---

# CL2. `timeout_while_release_budget_is_exhausted`

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
- **Пункт:** `CL2` — `timeout_while_release_budget_is_exhausted`.
- **Критерий:** Отстающий release cursor не сокращает новый timeout.
- **Статус в архивном плане:** ✅.
