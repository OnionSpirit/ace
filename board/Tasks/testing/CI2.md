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
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.14 futures/timeout.h.
- **Fixture:** `clock_initialization_fixture`.
- **Пункт:** `CI2` — `partial_initialization_failure_rethrows_and_retries`.
- **Критерий:** Частично созданный wheel уничтожается; последующий timeout строит полное состояние.
- **Статус в архивном плане:** ✅.
