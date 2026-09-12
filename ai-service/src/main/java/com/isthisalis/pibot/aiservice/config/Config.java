package com.isthisalis.pibot.aiservice.config;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestClient;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.dataformat.toml.TomlMapper;

import lombok.NonNull;
import lombok.RequiredArgsConstructor;
import lombok.ToString;

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

    /**
     * Loads the AI configuration from the shared TOML config
     * ({@code CONFIG_PATH}, default {@code /app/config.toml}).
     *
     * <p>Expected layout (see {@code config.toml}):
     * <pre>
     * [ai]
     * provider = "groq"
     * api_key = "$GROQ_API_KEY"
     * rules = "..."   # literal, "file:/path", or inline multi-line
     * bio = "..."     # literal, "file:/path", or inline multi-line
     *
     * [ai.providers.groq]
     * api_url = "https://..."
     * model = "..."
     * </pre>
     */
    @Bean
    public static com.isthisalis.ailib.api.Configuration reload() {
        try {
            TomlMapper mapper = TomlMapper.builder()
                    .propertyNamingStrategy(PropertyNamingStrategies.SNAKE_CASE)
                    .build();

            String path = System.getenv().getOrDefault("CONFIG_PATH", "/app/config.toml");
            AiTomlConfig toml = mapper.readValue(new File(path), AiTomlConfig.class);

            Ai ai = toml.getAi();
            String providerName = ai.getProvider();
            Provider provider = ai.getProviders() != null ? ai.getProviders().get(providerName) : null;
            if (provider == null) {
                log.atError().log("AI provider '{}' not found in {} under [ai.providers]", providerName, path);
                return new com.isthisalis.ailib.api.Configuration("", "", "", "", "");
            }

            String apiKey = tryEnv(ai.getApiKey());
            String apiUrl = tryEnv(provider.getApiUrl());
            String model = tryEnv(provider.getModel());
            String rules = valOrFile(ai.getRules());
            String bio = valOrFile(ai.getBio());

            log.atInfo().log("AI config loaded: provider={}, model={}, rules={} chars, bio={} chars",
                    providerName, model, rules == null ? 0 : rules.length(), bio == null ? 0 : bio.length());

            return new com.isthisalis.ailib.api.Configuration(apiKey, apiUrl, model, rules, bio);
        } catch (Exception e) {
            log.atError().log("Error in config reloading! " + e);
            return new com.isthisalis.ailib.api.Configuration("", "", "", "", "");
        }
    }

    /**
     * Returns the literal value, or the contents of the referenced file when the
     * value starts with {@code file:} (e.g. {@code file:/app/rules.txt}). The file
     * is re-read on every reload, so edits on disk apply without a rebuild. On
     * read failure the literal value is kept.
     */
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

    /** TOML root: only the {@code [ai]} table is consumed by this service. */
    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class AiTomlConfig {
        private Ai ai;

        public Ai getAi() { return ai; }
        public void setAi(Ai ai) { this.ai = ai; }
    }

    /** The {@code [ai]} table and its {@code [ai.providers.*]} sub-tables. */
    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class Ai {
        private String apiKey;
        private String provider;
        private String rules;
        private String bio;
        private Map<String, Provider> providers;

        public String getApiKey() { return apiKey; }
        public void setApiKey(String apiKey) { this.apiKey = apiKey; }

        public String getProvider() { return provider; }
        public void setProvider(String provider) { this.provider = provider; }

        public String getRules() { return rules; }
        public void setRules(String rules) { this.rules = rules; }

        public String getBio() { return bio; }
        public void setBio(String bio) { this.bio = bio; }

        public Map<String, Provider> getProviders() { return providers; }
        public void setProviders(Map<String, Provider> providers) { this.providers = providers; }
    }

    /** A single provider entry under {@code [ai.providers.X]}. */
    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class Provider {
        private String apiUrl;
        private String model;

        public String getApiUrl() { return apiUrl; }
        public void setApiUrl(String apiUrl) { this.apiUrl = apiUrl; }

        public String getModel() { return model; }
        public void setModel(String model) { this.model = model; }
    }
}