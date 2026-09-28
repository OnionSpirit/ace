---
id: "testing:FS5"
source_id: "FS5"
kind: "coverage"
status: "Решено"
source_status: "✅ (в file_write_and_read)"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 843
---

# FS5. `file_write`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.19 fs.h.
- **Fixture:** `fs_fixture`.
- **Пункт:** `FS5` — `file_write`.
- **Критерий:** write() через file_link.
- **Статус в архивном плане:** ✅ (в file_write_and_read).
