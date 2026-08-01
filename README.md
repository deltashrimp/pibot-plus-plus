# PiBot

Telegram bot system based on microservices: **Core**, **RP**, **AI**, and
**Auto-Mod**, backed by PostgreSQL and Redis, orchestrated with Docker Compose.

---

## Prerequisites

- Docker Engine 24+ and Docker Compose v2
- A `.env` file with real secrets (see `.env`)
- A Telegram `api_id` / `api_hash` from https://my.telegram.org

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

Services communicate on the `pibot-net` Docker network and are not exposed to
the host (no ports are published). `core` and `ai` need outbound access to the
Telegram / LLM APIs, so the network is not `internal`. Reach the internal APIs
by running `docker exec` inside a container (see PROJECT.md).

## Healthchecks

- `postgres`: `pg_isready`
- `redis`: `redis-cli ping`
- `core`: `curl -f http://localhost:8080/health`
- `rp`: `curl -f http://localhost:8081/health`
- `ai`: HTTP `GET /health`
- `auto-mod`: `curl -f http://localhost:8083/health`
