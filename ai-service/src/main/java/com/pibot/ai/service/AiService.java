package com.pibot.ai.service;

import com.pibot.ai.model.RequestResponse.AiRequest;
import com.pibot.ai.model.RequestResponse.AiResponse;
import org.springframework.stereotype.Service;

import java.util.List;

/**
 * Placeholder AI service.
 *
 * <p>TODO: implement real LLM calls (Groq / OpenRouter) with tool calling,
 * request/usage accounting, and Core configuration reload.
 */
@Service
public class AiService {

    public AiResponse generate(AiRequest request) {
        return new AiResponse(
                "Hello from Java!",
                "dummy",
                0,
                0,
                0,
                List.of());
    }

    /** No-op placeholder; providers will be (re)loaded from Core later. */
    public void reloadConfig() {
        // intentionally empty
    }
}
