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

---

## Endpoints 
**/health** -> Check service status. Request method: GET

**/reload_config** -> Reloads AI configuration. Request method: POST

**/ai/ask** -> Sends request to AI model. Request method: POST. Return type: String
