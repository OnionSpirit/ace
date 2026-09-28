---
id: "benchmark:BM1"
source_id: "BM1"
kind: "benchmark"
status: "Решено"
source_status: "Реализованная benchmark-сценарий"
priority: "Не указан"
source_document: "agents/BENCHMARKS.md"
source_line: 70
---

# BM1. bm_cutex_race_capture

```dispatch
label: Взять задачу
intent: take-task
tool: opencode
repo: ace
prompt: >-
  Возьми задачу {{id}} из карточки {{vault}}/{{file}} проекта ACE. Рабочая папка — корень репозитория ACE. Прочитай AGENTS.md, эту карточку и архивные документы, указанные в её frontmatter. Доска — активный реестр статусов и планов. Начни с read-only исследования, уточни неясности и представь план. Не меняй код, тесты и документацию до явного утверждения плана. Нажатие кнопки запускает обсуждение, а не утверждает неизвестный план. Обращайся к пользователю как к уважаемому Glorious ACE Developer. Отвечай по-русски.
```

## Архивная запись

- **Сценарий:** `bm_cutex_race_capture`.
- **Что измеряет:** Пропускная способность cutex capture/release, 8 runner-ов x 100k..
- **Статус в архивном инвентаре:** реализованная benchmark-сценарий.
