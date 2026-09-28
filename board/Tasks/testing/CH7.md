---
id: "testing:CH7"
source_id: "CH7"
kind: "coverage"
status: "Открыто"
source_status: "⬜"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 740
---

# CH7. `pull_suspends`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.15 futures/channel.h.
- **Fixture:** `channel_extra_fixture` (расширение).
- **Пункт:** `CH7` — `pull_suspends`.
- **Критерий:** pull на пустом канале → корутина суспендится.
- **Статус в архивном плане:** ⬜.
