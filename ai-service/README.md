# AI Service

## Configuration

src/main/resources/application-ai.yaml:
```YAML
api-key: "AI API provider api key" # Type: String
api-url: "AI API endpoint" # Type: String
model: "AI model" # Type: String

rules: "AI answering ruleset" # Type: String
bio: "AI personality" # Type: String
```

`rules` and `bio` are inserted into the AI's system prompt. Instead of a literal
value you can load them from a file on disk with the `file:` prefix, e.g.
`rules: "file:/app-config/ruleset.md"` (absolute path recommended; relative paths
resolve against the working dir). The files are re-read on startup and on every
`POST /reload_config`, so edits apply without a rebuild. In Docker,
`./ai-service/src/main/resources` is mounted at `/app-config`.

---

## Endpoints 
All endpoints except `/health` require the `X-API-Key` header to match the
service key (`ai-request-key`, env `AI_API_KEY`).

**/health** -> Check service status. Request method: GET. No auth.

**/reload_config** -> Reloads AI configuration. Request method: POST. Auth: X-API-Key.

**/ai/ask** -> Sends request to AI model. Request method: POST. Auth: X-API-Key.
Body: `{"message": "..."}`. Return type: String (JSON: `{"response": "..."}`).
