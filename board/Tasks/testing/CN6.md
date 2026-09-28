---
id: "testing:CN6"
source_id: "CN6"
kind: "coverage"
status: "Открыто"
source_status: "⬜"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 876
---

# CN6. `stdin_link`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.20 console.h.
- **Fixture:** `console_fixture`.
- **Пункт:** `CN6` — `stdin_link`.
- **Критерий:** stdin_link() возвращает валидный link.
- **Статус в архивном плане:** ⬜.
