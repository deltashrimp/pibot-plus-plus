package com.isthisalis.pibot.aiservice.config;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.io.ClassPathResource;
import org.springframework.web.client.RestClient;

import lombok.NonNull;
import lombok.RequiredArgsConstructor;
import lombok.ToString;
import tools.jackson.databind.ObjectMapper;
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
    public static com.isthisalis.ailib.api.Configuration reload() {
        ObjectMapper mapper = YAMLMapper.builder().propertyNamingStrategy(PropertyNamingStrategies.KEBAB_CASE).build();

        try {
            com.isthisalis.ailib.api.Configuration config = mapper.readValue(new ClassPathResource("application-ai.yaml").getInputStream(),
             com.isthisalis.ailib.api.Configuration.class);

             String bio = valOrFile(config.getBio());
             String rules = valOrFile(config.getRules());

             String model = tryEnv(config.getModel());
             String apiKey = tryEnv(config.getApiKey());
             String apiUrl = tryEnv(config.getApiUrl());

             return new com.isthisalis.ailib.api.Configuration(apiKey, apiUrl, model, rules, bio);
        } catch (IOException e) {
            log.atError().log("Error in config reloading! " + e);
            return new com.isthisalis.ailib.api.Configuration("", "", "", "", "");
        }
    }

    /*@Bean
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
                envOr("GROQ_API_KEY", base.getApiKey()),
                envOr("GROQ_API_URL", base.getApiUrl()),
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
     */ /* 
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
    }*/

    private static String valOrFile(@NonNull String val) {
        if (val.startsWith("file:")) {
            Path path = Path.of(val.substring(5));
            if (Files.exists(path) && Files.isReadable(path)) {
                try {
                    return new String(Files.readAllBytes(path));
                } catch (IOException e) {
                    log.atError().log("Error while loading file: {}. Error: {}", path.toString(), e);
                    return val;
                }
            }
            return val;
        }
        return val;
    }

    private static String tryEnv(String val) {
        if (val.startsWith("$")) { return System.getenv(val.substring(1)); }
        else return val;
    }
}