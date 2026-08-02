package com.pibot.ai.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestClient;

/**
 * Application beans.
 */
@Configuration
public class AppConfig {

    @Value("${ai.core-api-url}")
    private String coreApiUrl;

    /** Pre-configured client for calling Core's internal REST API. */
    @Bean
    public RestClient coreRestClient(RestClient.Builder builder) {
        return builder.baseUrl(coreApiUrl).build();
    }
}
