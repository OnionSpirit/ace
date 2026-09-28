---
id: "testing:NQ4"
source_id: "NQ4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 759
---

# NQ4. `mpsc_node_api_reuses_live_storage`

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
- **Fixture:** `nukes_concurrency_fixture`.
- **Пункт:** `NQ4` — `mpsc_node_api_reuses_live_storage`.
- **Критерий:** Внешний node ownership и восстановленный payload lifetime.
- **Статус в архивном плане:** ✅.
