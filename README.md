# PiBot Microservices

Telegram bot system based on microservices: **Core**, **RP**, and **AI**, backed by
PostgreSQL and Redis, orchestrated with Docker Compose.

## Architecture

| Service    | Language | Stack                                             | Port (internal) |
| ---------- | -------- | ------------------------------------------------- | --------------- |
| `core`     | C++17    | TDLib, oatpp, oatpp-pqsql, libpqxx, spdlog         | 8080            |
| `rp-service` | C++17  | oatpp, oatpp-pqsql, libpqxx, redis-plus-plus, spdlog | 8081         |
| `ai-service` | Python | FastAPI, instructor, openai, python-json-logger, httpx, pydantic | 8082 |
| `postgres` | -        | PostgreSQL 15                                      | -               |
| `redis`    | -        | Redis 7                                            | -               |

```
+----------------------------------+    (internal Docker network)
|  Telegram API                    |
+----------------------------------+
|  core  <---->  postgres          |
|    ^    <---->  redis            |
|    |                             |
|    +---->  rp-service            |
|    +---->  ai-service            |
+----------------------------------+
```

## Project layout

```
core/            C++ core service (TDLib bot engine, config API, DB manager)
rp-service/      C++ reporting service (caching via Redis, config client)
ai-service/      Python AI service (FastAPI, LLM client, tools, telemetry)
scripts/         Build helpers
docker-compose.yml
.env             Secrets and configuration (dummy values for local use)
```

## Prerequisites

- Docker Engine 24+ and Docker Compose v2
- A `.env` file with real secrets (see `.env`)

## Build and run

```bash
# Build all images
docker compose build

# Or via the helper script
./scripts/build.sh

# Start the whole stack
docker compose up -d

# Follow logs
docker compose logs -f

# Stop everything
docker compose down

# Stop and remove data volumes
docker compose down -v
```

All services communicate on the internal network `pibot-net` and are not exposed
externally. The `core` service port is mapped to the host **only for debugging**
— remove the `ports:` entry under `core` in `docker-compose.yml` to keep it fully
internal.

## Healthchecks

- `postgres`: `pg_isready`
- `redis`: `redis-cli ping`
- `core`: `curl -f http://localhost:8080/health`
- `rp`: `curl -f http://localhost:8081/health`
- `ai`: HTTP `GET /health`

## Notes

- This repository currently contains only the project skeleton and Docker
  environment; application code is intentionally left as placeholders.
- The C++ services require system packages (TDLib, oatpp, redis-plus-plus, ...)
  that are installed inside the Docker images; `core/Dockerfile` builds TDLib
  from source because it is not packaged for Debian.
