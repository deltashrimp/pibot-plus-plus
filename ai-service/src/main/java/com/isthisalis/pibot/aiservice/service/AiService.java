package com.isthisalis.pibot.aiservice.service;

import org.springframework.stereotype.Service;

import com.isthisalis.pibot.aiservice.model.RequestResponse.AiRequest;
import com.isthisalis.pibot.aiservice.model.RequestResponse.AiResponse;

import java.util.List;

/**
 * Placeholder AI service.
 *
 * <p>TODO: implement real LLM calls (Groq / OpenRouter) with tool calling,
 * request/usage accounting, and Core configuration reload.
 */
@Service
@Deprecated
public class AiService {

    @Deprecated
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
    @Deprecated
    public void reloadConfig() {
        // intentionally empty
    }
}
