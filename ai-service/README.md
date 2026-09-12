# AI Service

## Configuration

AI settings live in the shared `config.toml` (`[ai]` and `[ai.providers.*]`
tables), read from `CONFIG_PATH` (default `/app/config.toml`, mounted by
Docker Compose). `config.toml` is gitignored and holds ALL settings (general +
AI, including private rulesets/bio); `public-config.toml` holds the shared
defaults and is copied into it by `scripts/build.sh`. See the comments in
`config.toml`.

```TOML
[ai]
provider = "groq"                     # active provider key
api_key = "$GROQ_API_KEY"             # literal or "$ENV_VAR" reference
rules = "file:/app-config/ruleset.md" # literal, "file:/path", or inline
bio = "file:/app-config/bio.md"       # literal, "file:/path", or inline

[ai.providers.groq]
api_url = "https://api.groq.com/openai/v1/chat/completions"
model = "openai/gpt-oss-120b"
```

`rules` and `bio` are inserted into the AI's system prompt. Values can be
literal strings, or loaded from disk with the `file:` prefix, e.g.
`rules: "file:/app-config/ruleset.md"` (absolute path recommended; relative
paths resolve against the working dir). The config is re-read on startup and on
every `POST /reload_config`, so edits apply without a rebuild. In Docker,
`./config.toml` (private, merged from `public-config.toml` by
`scripts/build.sh`) is mounted at `/app/config.toml`.

---

## Endpoints 
All endpoints except `/health` require the `X-API-Key` header to match the
service key (`ai-request-key`, env `AI_API_KEY`).

**/health** -> Check service status. Request method: GET. No auth.

**/reload_config** -> Reloads AI configuration. Request method: POST. Auth: X-API-Key.

**/ai/ask** -> Sends request to AI model. Request method: POST. Auth: X-API-Key.
Body: `{"message": "..."}`. Return type: String (JSON: `{"response": "..."}`).
