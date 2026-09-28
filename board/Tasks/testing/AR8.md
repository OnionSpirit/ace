---
id: "testing:AR8"
source_id: "AR8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 952
---

# AR8. `limit_zero_drains_every_alloc`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.22 core/arena.h — arena_fixture.
- **Fixture:** `backup_fixture` (добавлен).
- **Пункт:** `AR8` — `limit_zero_drains_every_alloc`.
- **Критерий:** max=0 дренирует канал перед каждой аллокацией.
- **Статус в архивном плане:** ✅.
