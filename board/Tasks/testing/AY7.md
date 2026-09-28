---
id: "testing:AY7"
source_id: "AY7"
kind: "coverage"
status: "Открыто"
source_status: "⬜ (удалён)"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 601
---

# AY7. `any_copy`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_any_fixture`.
- **Пункт:** `AY7` — `any_copy`.
- **Критерий:** Копирование запрещено (shallow-copy → double-free).
- **Статус в архивном плане:** ⬜ (удалён).
