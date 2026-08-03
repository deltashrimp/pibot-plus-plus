package com.isthisalis.pibot.aiservice.model;

import java.util.List;
import java.util.Map;

/**
 * Request/response DTOs for the AI service REST API.
 *
 * <p>Snake-case field names match the wire contract expected by the Core
 * service (e.g. {@code chat_id}, {@code tool_calls}). Records keep the DTOs
 * immutable without requiring Lombok.
 */
@Deprecated
public final class RequestResponse {

    private RequestResponse() {
    }

    /** One chat turn passed to the LLM. */
    @Deprecated
    public record AiMessage(String role, String content) {
    }

    /** Result of an LLM tool invocation (optional, for future tool calling). */
    @Deprecated
    public record AiToolCall(String id, String type, String function) {
    }

    /** Request body for {@code POST /ai/generate}. */
    @Deprecated
    public record AiRequest(
            //@NonNull Long chat_id,
            //@NotNull Long user_id,
            //@NotNull @NotEmpty List<AiMessage> messages,
            List<Map<String, Object>> tools) {
    }

    /** Response body for {@code POST /ai/generate}. */
    @Deprecated
    public record AiResponse(
            String content,
            String model,
            Integer prompt_tokens,
            Integer completion_tokens,
            Integer total_tokens,
            List<AiToolCall> tool_calls) {
    }

    /** Uniform error body. */
    @Deprecated
    public record ErrorResponse(String error) {
    }
}
