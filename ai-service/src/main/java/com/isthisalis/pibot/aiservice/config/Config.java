package com.isthisalis.pibot.aiservice.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestClient;

/**
 * Application beans.
 */
@Configuration
public class Config {

    @Value("${ai.core-api-url}")
    private String coreApiUrl;

    @Value("${ai.api-key}")
    private String apiKey;

    @Value("${ai.api-url}")
    private String apiUrl;

    /** Pre-configured client for calling Core's internal REST API. */
    @Bean
    public RestClient coreRestClient(RestClient.Builder builder) {
        return builder.baseUrl(coreApiUrl).build();
    }
}
