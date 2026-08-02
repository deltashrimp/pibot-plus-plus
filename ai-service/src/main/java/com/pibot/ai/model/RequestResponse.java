package com.pibot.ai.model;

import jakarta.validation.constraints.NotEmpty;
import jakarta.validation.constraints.NotNull;

import java.util.List;
import java.util.Map;

/**
 * Request/response DTOs for the AI service REST API.
 *
 * <p>Snake-case field names match the wire contract expected by the Core
 * service (e.g. {@code chat_id}, {@code tool_calls}). Records keep the DTOs
 * immutable without requiring Lombok.
 */
public final class RequestResponse {

    private RequestResponse() {
    }

    /** One chat turn passed to the LLM. */
    public record AiMessage(String role, String content) {
    }

    /** Result of an LLM tool invocation (optional, for future tool calling). */
    public record AiToolCall(String id, String type, String function) {
    }

    /** Request body for {@code POST /ai/generate}. */
    public record AiRequest(
            @NotNull Long chat_id,
            @NotNull Long user_id,
            @NotNull @NotEmpty List<AiMessage> messages,
            List<Map<String, Object>> tools) {
    }

    /** Response body for {@code POST /ai/generate}. */
    public record AiResponse(
            String content,
            String model,
            Integer prompt_tokens,
            Integer completion_tokens,
            Integer total_tokens,
            List<AiToolCall> tool_calls) {
    }

    /** Uniform error body. */
    public record ErrorResponse(String error) {
    }
}
