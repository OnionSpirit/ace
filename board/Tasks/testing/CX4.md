---
id: "testing:CX4"
source_id: "CX4"
kind: "coverage"
status: "Открыто"
source_status: "⬜"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 773
---

# CX4. `cutex_sync_wakes`

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Раздел:** 3.16 futures/cutex.h.
- **Fixture:** `cutex_extra_fixture` (расширение).
- **Пункт:** `CX4` — `cutex_sync_wakes`.
- **Критерий:** sync() пробуждает следующего ожидающего.
- **Статус в архивном плане:** ⬜.
