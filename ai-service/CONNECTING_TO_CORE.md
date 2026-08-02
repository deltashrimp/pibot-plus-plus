# Интеграция AI-модуля с Core-модулем

Руководство для разработчика: как AI-сервис (`ai-service/`) подключается к
Core-модулю (`core/`) и как вызывать его API.

---

## 1. Общая схема

```
Telegram  <-->  core (C++/oatpp, :8080)  <-->  ai-service (Java/Spring, :8082)
                     |  ^                        ^  |
                     |  | X-API-Key               |  | X-API-Key
                     v  |                        |  v
                PostgreSQL/Redis            LLM API (Groq / OpenRouter)
```

- **Core** — основной бот (C++), «дергает» AI по REST: `POST /ai/generate`.
- **AI-сервис** — обёртка над LLM: принимает запрос от Core, возвращает ответ.
- В обратную сторону AI-сервис умеет ходить в Core (`ai.core-api-url`) за
  конфигом (rank/mute и т.п.).
- Оба контейнера в одной Docker-сети `pibot-net` и видят друг друга **по имени
  сервиса** (`core`, `ai`), порты наружу для этого не публикуются.

---

## 2. Что уже реализовано (skeleton)

AI-сервис — **заглушка**: логика вызова LLM ещё не написана. Сейчас
`POST /ai/generate` всегда возвращает фиксированный ответ
`{"content": "Hello from Java!", "model": "dummy", ...}`. Реализация — в
`src/main/java/com/pibot/ai/service/AiService.java` (метод `generate`).

Откуда брать настоящую генерацию: в `docker-compose.yml` уже прокинуты ключи
`GROQ_API_KEY` и `OPENROUTER_API_KEY` (env `ai-service`). Сейчас их никто не
читает.

---

## 3. Конфигурация (env-переменные)

Значения задаются в `.env` в корне репозитория и подставляются в
`docker-compose.yml`:

| Переменная      | Где используется               | Значение по умолчанию |
|-----------------|--------------------------------|-----------------------|
| `AI_PORT`       | порт HTTP AI-сервиса           | `8082`                |
| `AI_API_KEY`    | ключ для `POST /ai/generate` и `POST /reload_config` | `default-key` (в проде генерится случайно) |
| `CORE_HOST`     | DNS-имя Core в compose         | `core`                |
| `CORE_PORT`     | порт Core                      | `8080`                |
| `CORE_API_KEY`  | ключ, который AI шлёт в Core   | —                     |
| `GROQ_API_KEY` / `OPENROUTER_API_KEY` | для будущей LLM-генерации | — |

В `src/main/resources/application.yml`:

```yaml
ai:
  core-api-url: ${CORE_API_URL:http://${CORE_HOST:core}:${CORE_PORT:8080}}
  api-key:      ${AI_API_KEY:default-key}
```

- `ai.api-key` — ожидаемый `X-API-Key` от Core.
- `ai.core-api-url` — адрес Core, куда AI ходит за данными (готовый бин
  `RestClient coreRestClient` в `src/main/java/com/pibot/ai/config/AppConfig.java`).

---

## 4. API AI-сервиса (что вызывает Core)

Базовый URL внутри сети: `http://ai:8082` (порт = `AI_PORT`).

### `GET /health` — публичный (healthcheck)

Ответ: `{"status": "ok"}` (200). Используется Docker-хелсчеком, ключ не нужен.

### `POST /ai/generate` — требует заголовок `X-API-Key`

Заголовок должен быть равен `AI_API_KEY`, иначе **401**.

**Тело запроса** (поля snake_case — важны для контракта!):

```json
{
  "chat_id": 123456789,
  "user_id": 987654321,
  "messages": [
    {"role": "system", "content": "Ты — модератор чата"},
    {"role": "user",   "content": "Привет"}
  ],
  "tools": []              // опционально, для будущего tool-calling
}
```

Обязательные поля: `chat_id` (Long), `user_id` (Long),
`messages` (непустой массив `{role, content}`). Если их нет — **400**.

**Ответ (200):**

```json
{
  "content": "Hello from Java!",
  "model": "dummy",
  "prompt_tokens": 0,
  "completion_tokens": 0,
  "total_tokens": 0,
  "tool_calls": []
}
```

`tool_calls` — массив объектов `{id, type, function}` (зарезервировано под
инструменты, сейчас всегда пустой).

DTO объявлены в
`src/main/java/com/pibot/ai/model/RequestResponse.java` (Java-записи
`AiRequest`, `AiResponse`, `AiToolCall`, `AiMessage`, `ErrorResponse`).

### `POST /reload_config` — требует `X-API-Key`

Перезагружает конфиг (сейчас заглушка, всегда 200). Вызывается из Core когда
конфигурация чата изменилась.

---

## 5. Как Core вызывает AI (пример)

Пример запроса из Core (внутри сети `pibot-net`):

```bash
curl -X POST http://ai:8082/ai/generate \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: '"$AI_API_KEY" \
  -d '{
    "chat_id": 123456789,
    "user_id": 987654321,
    "messages": [{"role": "user", "content": "Привет"}]
  }'
```

Код-статусы, которые стоит обрабатывать в Core:

| Код | Когда |
|-----|-------|
| 200 | успех, в теле `content` |
| 400 | некорректное тело запроса |
| 401 | нет/неверный `X-API-Key` |
| 5xx | AI-сервис не готов (нет ответа LLM и т.п.) — повторить с backoff |

---

## 6. Как AI ходит в Core (обратное направление)

За это отвечает `coreRestClient` (бин `RestClient`), настроенный на
`ai.core-api-url`. Заголовок авторизации `X-API-Key` должен браться из
`CORE_API_KEY`. Известные эндпоинты Core (`core/src/api/api_controller.h`):

| Метод/путь | Описание |
|------------|----------|
| `GET /health` | статус Core (публичный) |
| `GET /config/global_bans` | список глобальных банов |
| `GET /config/chat/{chatId}/rank/{userId}` | ранг пользователя в чате |
| `GET /config/chat/{chatId}/mute/{userId}` | информация о муте |

Все эндпоинты, кроме `/health`, требуют заголовок `X-API-Key` со значением
`CORE_API_KEY`.

> Если Core ещё не добавил какой-то нужный эндпоинт — его надо добавить в
> `core/src/api/api_controller.h` (фреймворк oatpp).

---

## 7. Локальный запуск и проверка

```bash
# 1. Собрать образ
docker build -t pibot-ai ./ai-service

# 2. Запустить контейнер (порт наружу — только для локального теста)
KEY=$(grep '^AI_API_KEY=' .env | cut -d= -f2)
docker run -d --name ai-test -p 18082:8082 \
  -e AI_PORT=8082 -e AI_API_KEY="$KEY" pibot-ai

# 3. Проверить
curl -s http://localhost:18082/health                                  # {"status":"ok"}
curl -s -X POST http://localhost:18082/ai/generate \
  -H 'Content-Type: application/json' -H "X-API-Key: $KEY" \
  -d '{"chat_id":1,"user_id":2,"messages":[{"role":"user","content":"hi"}]}'  # 200
curl -s -o /dev/null -w '%{http_code}\n' -X POST http://localhost:18082/ai/generate \
  -d '{"chat_id":1,"user_id":2,"messages":[]}'                         # 401 (нет ключа)

# 4. Убрать тестовый контейнер
docker rm -f ai-test
```

Полный стек поднимается через `docker compose up --build` (сервис `ai` ждёт
`core` в статусе healthy — см. `depends_on` в `docker-compose.yml`).

---

## 8. Логирование

Все логи — JSON в stdout (см. `src/main/resources/logback-spring.xml`), поля:
`ts`, `level`, `logger`, `message`. Для запросов `/ai/generate` в MDC кладутся
`user_id`, `chat_id`, `duration_ms`, они попадают в JSON-строку лога:

```json
{"ts":"...","level":"INFO","logger":"com.pibot.ai.controller.AiController",
 "message":"generated response for user 987654321",
 "user_id":"987654321","chat_id":"123456789","duration_ms":"0"}
```

---

## 9. Куда смотреть дальше

1. Реализовать настоящий вызов LLM в `AiService.generate` (Groq/OpenRouter по
   ключам из env), вернуть реальный `content` и `model`.
2. При необходимости добавить tool-calling (`tools` в запросе, `tool_calls` в
   ответе) — DTO уже готовы.
3. Если AI будет дергать Core за конфигом — использовать `coreRestClient` +
   `CORE_API_KEY` (эндпоинты Core из §6).
4. Тесты: `mvn test` (сейчас пустой smoke-тест контекста), покрыть
   контроллер и контракт.
