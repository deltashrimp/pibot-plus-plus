# PiBot — Project Details

Technical documentation for the PiBot microservices system: architecture, the
`core` microservice, and the overall project layout.

---

## Architecture

| Service     | Language | Stack                                                | Port (internal) |
| ----------- | -------- | ---------------------------------------------------- | --------------- |
| `core`      | C++17    | TDLib, oatpp, libpqxx, spdlog                        | 8080            |
| `rp-service`| C++17    | oatpp, hiredis, redis-plus-plus, spdlog              | 8081            |
| `ai-service`| Java 17   | Spring Boot 3.2 (Maven), RestClient, logstash-logback | 8082 |
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
|    |---->  rp-service            |
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
| `RP_SERVICE_URL`   | `http://rp:8081` | RP service base URL (empty disables RP) |
| `RP_API_KEY`       | -          | Secret shared with the RP service (`X-API-Key`) |
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

The bot never changes Telegram admin status; admin rights are managed manually
by the group's admins. Before applying a rank, `/rank` reads the target's
current Telegram admin status and enforces that it matches: ranks `2` and `3`
can be given only to users who are already Telegram admins, and rank `4` only
to users who are not (their admin rights must first be removed manually).

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
| `/rank < 2/ 3 / 4 > < target >` | owner/admin+ (1-2) | Set a user's rank; 2/3 require the target to already be a Telegram admin, 4 requires them not to be; rank 1 cannot be given (giving 2 requires owner). The bot never changes Telegram admin status — that is done manually |
| `/ranks`               | admin (3) | List all users with ranks 1-3              |
| `/rpadd < trigger > < response >` | owner (1) | Add an RP command (first word = trigger)   |
| `/rpremove < trigger >` | owner (1) | Remove an RP command                       |
| `/rpedit < trigger > < response >` | owner (1) | Update an RP command's response            |
| `/rplist`              | owner (1) | List the chat's RP commands                |

`<target>` is `@username`, a numeric user ID, or a reply to a message.
Globally banned users are silently ignored before any command parsing.

Non-command messages are forwarded to the RP service after the global-ban
check; if the message starts with a stored trigger, the bot replies with the
matching response (before any AI processing). Responses support the
placeholders `{mention}` / `{mention1}` (the message author) and `{mention2}`
(the user being replied to, if any), substituted with clickable mentions.

Bot messages are localized in Russian; `/start` replies with a plain-text
greeting, all other replies are sent with markdown rendering.

---

## RP service

Stores per-chat RP (role-play) commands in Redis hashes
(`rp:commands:{chat_id}`, field = trigger, value = response template) and
matches the first word of a message against them. It has no Telegram access;
the `core` service calls it over the internal network.

### Layout

```
rp-service/
├── src/
│   ├── main.cpp                 entry point: Redis check + predefined load + oatpp server
│   ├── api/api_controller.{h,cpp}    REST endpoints (X-API-Key protected)
│   ├── service/rp_service.{h,cpp}    match + command management + placeholders
│   ├── service/command_matcher.h     trigger extraction (first word)
│   ├── service/variable_substitutor.h  {mention}/{mention1}/{mention2} substitution
│   ├── storage/redis_storage.{h,cpp} Redis hash access via redis-plus-plus
│   ├── logging/logger.{h,cpp}        spdlog with JSON output to stdout
│   └── model/rp_command.h            Command / MatchResult / CommandResult structs
└── rp_phrases.json               predefined commands (trigger -> response)
```

### Environment variables

| Variable            | Default                 | Description                              |
| ------------------- | ----------------------- | ---------------------------------------- |
| `REDIS_HOST`        | `redis`                 | Redis host                               |
| `REDIS_PORT`        | `6379`                  | Redis port                               |
| `RP_PORT`           | `8081`                  | oatpp server port                        |
| `RP_API_KEY`        | -                       | Secret required for all `/rp` endpoints (`X-API-Key`) |
| `RP_PHRASES_FILE`   | `/app/rp_phrases.json`  | JSON file with predefined commands       |
| `LOG_LEVEL`         | `info`                  | `debug` / `info` / `warn` / `error`      |

### REST API (internal network)

All `/rp` endpoints require `X-API-Key`; `/health` is public.

| Method | Path         | Body                                  | Description                    |
| ------ | ------------ | ------------------------------------- | ------------------------------ |
| GET    | `/health`    | -                                     | Health check                   |
| POST   | `/rp/match`  | `{chat_id, user_id, text, reply_to_user_id, mention1, mention2}` | Returns `{matched, response, trigger}` |
| POST   | `/rp/command`| `{action: add\|remove\|edit\|list, chat_id, trigger, response}` | Manage commands; `list` returns `{commands: {trigger: response}}` |

Predefined commands are seeded with `HSETNX` at startup and on first match for
a chat, so user customisations are never overwritten. Redis runs with AOF
enabled (`--appendonly yes`) so commands survive restarts.

---

## Project layout

```
core/            C++ core service (TDLib bot engine, REST API, DB manager)
rp-service/      C++ RP service (role-play commands in Redis)
ai-service/      Java 17 AI service (Spring Boot skeleton: REST API, structured logging)
auto-mod/        C++ auto-moderation skeleton (returns 200)
scripts/         Build helpers
docker-compose.yml
.env             Secrets and configuration (dummy values for local use)
```
