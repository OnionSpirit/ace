---
id: "testing:BK5"
source_id: "BK5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 900
---

# BK5. `backup_task_payload_awaited`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.21 futures/backup.h — backup / insure / emergency (добавлен).
- **Fixture:** `backup_fixture` (добавлен).
- **Пункт:** `BK5` — `backup_task_payload_awaited`.
- **Критерий:** task-коллбек co_await-ится до завершения (с его таймером), LIFO-смесь callable/task.
- **Статус в архивном плане:** ✅.
