package com.isthisalis.pibot.aiservice.controller;

import com.isthisalis.pibot.aiservice.api.Request;
import com.isthisalis.pibot.aiservice.api.Response;
import com.isthisalis.pibot.aiservice.service.AI;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpStatus;
import org.springframework.http.HttpStatusCode;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

/**
 * REST endpoints.
 *
 * <p>{@code POST /ai/ask} and {@code POST /reload_config} require the
 * {@code X-API-Key} header to match {@code ai-request-key} (env {@code AI_API_KEY});
 * {@code GET /health} is public so the Docker healthcheck can probe it.
 */
@RestController
public class AiController {

    private static final Logger log = LoggerFactory.getLogger(AiController.class);

    private final AI aiCaller;

    @Value("${ai-request-key}")
    private String aiRequestKey;

    public AiController(AI aiCaller) {
        this.aiCaller = aiCaller;
    }

    @PostMapping("/ai/ask")
    public ResponseEntity<Response> ask(
            @RequestHeader(value = "X-API-Key", required = false) String apiKeyHeader,
            @RequestBody Request request) {
        if (!authorized(apiKeyHeader)) return ResponseEntity.status(HttpStatusCode.valueOf(401)).build();

        long start = System.nanoTime();
        try {
            Response response = new Response(aiCaller.generate(request.getMessage()));

            MDC.put("duration_ms",
                    String.valueOf((System.nanoTime() - start) / 1_000_000));
            log.info("Generated response: ", response.getResponse());

            if (!response.getResponse().equals("none")) return ResponseEntity.ok(response);
            return ResponseEntity.ok(new Response(""));
        } catch (Exception e) {
            return ResponseEntity.status(HttpStatus.INTERNAL_SERVER_ERROR).build();
        } finally {
            MDC.clear();
        }
    }


    @PostMapping("/reload_config")
    public ResponseEntity<Void> reloadConfig(
            @RequestHeader(value = "X-API-Key", required = false) String apiKeyHeader) {
        if (!authorized(apiKeyHeader)) return ResponseEntity.status(HttpStatusCode.valueOf(401)).build();
        aiCaller.reloadConfig();
        return ResponseEntity.ok().build();
    }

    private boolean authorized(String apiKeyHeader) {
        if (apiKeyHeader == null || !apiKeyHeader.equals(aiRequestKey)) {
            return false;
        }
        return true;
    }
}
