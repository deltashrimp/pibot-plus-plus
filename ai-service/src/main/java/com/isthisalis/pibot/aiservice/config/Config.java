package com.isthisalis.pibot.aiservice.config;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.io.ClassPathResource;
import org.springframework.web.client.RestClient;

import lombok.RequiredArgsConstructor;
import lombok.ToString;
import tools.jackson.databind.PropertyNamingStrategies;
import tools.jackson.dataformat.yaml.YAMLMapper;

/**
 * Application beans.
 */
@Configuration
@RequiredArgsConstructor
@ToString
public class Config {

    private static final Logger log = LoggerFactory.getLogger(Config.class);

    /** 
     * API URL for core service.
     **/
    @Value("${core-api-url}")
    private String coreApiUrl; 


    /**
     * Pre-configured client for calling Core's internal REST API. 
     * @param builder RestClient builder.
     **/
    @Bean
    public RestClient coreRestClient() {
        return RestClient.builder().baseUrl(coreApiUrl).build();
    }

    @Bean
    public static com.isthisalis.ailib.api.Configuration reload() throws IOException {
        YAMLMapper yaml = YAMLMapper.builder().propertyNamingStrategy(PropertyNamingStrategies.KEBAB_CASE).build();
        com.isthisalis.ailib.api.Configuration base = yaml.readValue(
                new ClassPathResource("application-ai.yaml").getInputStream(),
                com.isthisalis.ailib.api.Configuration.class);
        String rules = textOr(base.getRules(), "rules");
        String bio = textOr(base.getBio(), "bio");
        log.info("AI config loaded: model={}, rules={} chars, bio={} chars",
                envOr("AI_MODEL", base.getModel()),
                rules == null ? 0 : rules.length(),
                bio == null ? 0 : bio.length());
        return new com.isthisalis.ailib.api.Configuration(
                envOr("OPENROUTER_API_KEY", base.getApiKey()),
                envOr("OPENROUTER_API_URL", base.getApiUrl()),
                envOr("AI_MODEL", base.getModel()),
                rules,
                bio);
    }

    private static String envOr(String name, String fallback) {
        String value = System.getenv(name);
        return (value == null || value.isEmpty()) ? fallback : value;
    }

    /**
     * Returns the literal value, or the contents of the referenced file when the
     * value starts with {@code file:} (e.g. {@code file:/app/rules.txt}). The file
     * is re-read on every reload, so edits on disk apply without a rebuild. On
     * read failure the literal value is kept.
     */
    private static String textOr(String value, String field) {
        if (value == null || !value.startsWith("file:")) {
            return value;
        }
        String path = value.substring("file:".length()).trim();
        if (path.isEmpty()) {
            return value;
        }
        try {
            return Files.readString(Paths.get(path));
        } catch (IOException e) {
            log.warn("Cannot read {} file '{}': {}; keeping literal value", field, path, e.getMessage());
            return value;
        }
    }

}
