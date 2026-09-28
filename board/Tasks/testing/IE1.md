---
id: "testing:IE1"
source_id: "IE1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 617
---

# IE1. `entity_default_construction`

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
- **Fixture:** `io_entity_fixture`.
- **Пункт:** `IE1` — `entity_default_construction`.
- **Критерий:** entity по умолчанию: _fd = -1, _is_closed = true.
- **Статус в архивном плане:** ✅.
