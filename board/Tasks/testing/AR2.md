---
id: "testing:AR2"
source_id: "AR2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 946
---

# AR2. `big_alloc_goes_to_malloc`

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
- **Пункт:** `AR2` — `big_alloc_goes_to_malloc`.
- **Критерий:** большая аллокация использует transient и сразу освобождается.
- **Статус в архивном плане:** ✅.
