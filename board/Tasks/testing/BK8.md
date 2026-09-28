---
id: "testing:BK8"
source_id: "BK8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 903
---

# BK8. `insure_dropped_when_next_op_ready`

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
- **Пункт:** `BK8` — `insure_dropped_when_next_op_ready`.
- **Критерий:** следующая операция готова синхронно (roaming) → страховка снята (механизм `_insured_prev`).
- **Статус в архивном плане:** ✅.
