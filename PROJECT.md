# PiBot — Project Details

Technical documentation for the PiBot microservices system: architecture, the
`core` microservice, and the overall project layout.

---

## Architecture

| Service     | Language | Stack                                                | Port (internal) |
| ----------- | -------- | ---------------------------------------------------- | --------------- |
| `core`      | C++17    | TDLib, oatpp, libpqxx, spdlog                        | 8080            |
| `rp-service`| C++17    | oatpp, oatpp-pqsql, libpqxx, redis-plus-plus, spdlog | 8081            |
| `ai-service`| Python   | FastAPI, instructor, openai, python-json-logger, httpx, pydantic | 8082 |
| `auto-mod`  | C++17    | oatpp (skeleton only)                                 | 8083            |
| `postgres`  | -        | PostgreSQL 15                                        | -               |
| `redis`     | -        | Redis 7                                              | -               |


```
+----------------------------------+
|  Telegram API                    |
+----------------------------------+
|  core  <---->  postgres          |
|    ^    <---->  redis            |
|    |                             |
|    +---->  rp-service            |
|    +---->  ai-service            |
|    +---->  auto-mod (skeleton)   |
+----------------------------------+
```

---

## Core microservice

The `core` service talks to Telegram via TDLib, enforces moderation commands,
stores bans/ranks/mutes in PostgreSQL, and exposes a REST API for the other
microservices.

### Layout

```
core/
├── src/
│   ├── main.cpp                 entry point: DB + TDLib thread + oatpp server
│   ├── tdlib/tdlib_client.{h,cpp}    TDLib wrapper (auth, event loop, requests)
│   ├── commands/command_handler.h    command interface + context
│   ├── commands/moderation_commands.{h,cpp}
│   ├── database/db_manager.{h,cpp}   PostgreSQL operations via libpqxx
│   ├── api/api_controller.{h,cpp}    REST endpoints (X-API-Key protected)
│   ├── logging/logger.{h,cpp}        spdlog with JSON output to stdout
│   └── utils/helpers.{h,cpp}
└── include/
```

### Environment variables

| Variable           | Default    | Description                              |
| ------------------ | ---------- | ---------------------------------------- |
| `TELEGRAM_TOKEN`   | -          | Bot token                                |
| `TELEGRAM_API_ID`  | `0`        | API id (my.telegram.org)                 |
| `TELEGRAM_API_HASH`| -          | API hash (my.telegram.org)               |
| `POSTGRES_HOST`    | `postgres` | PostgreSQL host                          |
| `POSTGRES_PORT`    | `5432`     | PostgreSQL port                          |
| `POSTGRES_DB`      | `pibot`    | Database name                            |
| `POSTGRES_USER`    | `pibot`    | Database user                            |
| `POSTGRES_PASSWORD`| -          | Database password                        |
| `CORE_API_KEY`     | -          | Secret for internal REST API (`X-API-Key`) |
| `CORE_PORT`        | `8080`     | oatpp server port                        |
| `LOG_LEVEL`        | `info`     | `debug` / `info` / `warn` / `error`      |

### REST API (internal network)

All endpoints require `X-API-Key` except `/health` (used by the Docker
healthcheck).

| Method | Path                                      | Description                       |
| ------ | ----------------------------------------- | --------------------------------- |
| GET    | `/health`                                 | Health check (no auth)            |
| GET    | `/config/global_bans`                     | List of globally banned user IDs  |
| GET    | `/config/chat/{chatId}/rank/{userId}`     | Rank of a user in a chat          |
| GET    | `/config/chat/{chatId}/mute/{userId}`     | Mute status of a user in a chat   |

`/health` returns `200` when PostgreSQL is reachable and TDLib is authorized,
and `503` otherwise.

Services are not exposed to the host, so call the API from inside the network:

```bash
docker exec pibot-core sh -c 'curl -H "X-API-Key: $CORE_API_KEY" http://localhost:8080/config/global_bans'
```

### Ranks

Stored in `chat_ranks` (`rank INT`): `0` developer (dev, global), `1` owner,
`2` admin+, `3` admin, `4` member (default when absent). Developers are listed
in the `devs` table (seeded with user `934151958`) and rank as `0` in every chat
regardless of `chat_ranks`.

### Commands

| Command      | Required rank | Action                                     |
| ------------ | ------------- | ------------------------------------------ |
| `/mute [ dur ] < target >` | admin (3) | Restrict a user; `dur` like `2m`, `1d`, `5y`; without `dur` = forever |
| `/unmute < target >`     | admin (3) | Lift restrictions                          |
| `/kick < target >`       | admin+ (2) | Kick (ban until now)                      |
| `/ban < target >`        | owner (1) | Ban with message revocation                |
| `/unban < target >`      | owner (1) | Unban                                      |
| `/globalban < target >`  | dev (0)  | Add to global ban list (all messages ignored) |
| `/globalunban < target >`| dev (0)  | Remove from global ban list                |
| `/rank < 2/ 3 / 4 > < target >` | owner/admin+ (1-2) | Set a user's rank; 3/2 grant Telegram admin, 4 removes it; rank 1 cannot be given (giving 2 requires owner) |
| `/ranks`               | admin (3) | List all users with ranks 1-3              |

`<target>` is `@username`, a numeric user ID, or a reply to a message.
Globally banned users are silently ignored before any command parsing.

Bot messages are localized in Russian; `/start` replies with a plain-text
greeting, all other replies are sent with markdown rendering.

---

## Project layout

```
core/            C++ core service (TDLib bot engine, REST API, DB manager)
rp-service/      C++ reporting service (caching via Redis, config client)
ai-service/      Python AI service (FastAPI, LLM client, tools, telemetry)
auto-mod/        C++ auto-moderation skeleton (returns 200)
scripts/         Build helpers
docker-compose.yml
.env             Secrets and configuration (dummy values for local use)
```
