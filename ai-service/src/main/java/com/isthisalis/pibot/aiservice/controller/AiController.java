package com.isthisalis.pibot.aiservice.controller;

import com.isthisalis.pibot.aiservice.model.RequestResponse.AiRequest;
import com.isthisalis.pibot.aiservice.model.RequestResponse.AiResponse;
import com.isthisalis.pibot.aiservice.service.AiService;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;

import java.util.Map;

/**
 * REST endpoints.
 *
 * <p>{@code POST /ai/generate} and {@code POST /reload_config} require the
 * {@code X-API-Key} header to match {@code ai.api-key} (env {@code AI_API_KEY});
 * {@code GET /health} is public so the Docker healthcheck can probe it.
 */
@RestController
public class AiController {

    private static final Logger log = LoggerFactory.getLogger(AiController.class);

    private final AiService aiService;

    @Value("${ai.api-key}")
    private String apiKey;

    public AiController(AiService aiService) {
        this.aiService = aiService;
    }

    @PostMapping("/ai/ask")
    public ResponseEntity<AiResponse> ask(
            @RequestHeader(value = "X-API-Key", required = false) String apiKeyHeader,
            @RequestBody AiRequest request) {
        requireApiKey(apiKeyHeader);

        //MDC.put("user_id", String.valueOf(request.user_id()));
        //MDC.put("chat_id", String.valueOf(request.chat_id()));
        long start = System.nanoTime();
        try {
            AiResponse response = aiService.generate(request);
            MDC.put("duration_ms",
                    String.valueOf((System.nanoTime() - start) / 1_000_000));
            //log.info("generated response for user {}", request.user_id());
            return ResponseEntity.ok(response);
        } finally {
            MDC.clear();
        }
    }

    @GetMapping("/health")
    public ResponseEntity<Map<String, String>> health() {
        return ResponseEntity.ok(Map.of("status", "ok"));
    }

    @PostMapping("/reload_config")
    public ResponseEntity<Void> reloadConfig(
            @RequestHeader(value = "X-API-Key", required = false) String apiKeyHeader) {
        requireApiKey(apiKeyHeader);
        aiService.reloadConfig();
        return ResponseEntity.ok().build();
    }

    private void requireApiKey(String apiKeyHeader) {
        if (apiKeyHeader == null || !apiKeyHeader.equals(apiKey)) {
            throw new ResponseStatusException(HttpStatus.UNAUTHORIZED, "Unauthorized");
        }
    }
}
