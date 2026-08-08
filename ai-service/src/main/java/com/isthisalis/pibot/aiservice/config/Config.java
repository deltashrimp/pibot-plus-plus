package com.isthisalis.pibot.aiservice.config;

import java.io.IOException;

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
        return yaml.readValue(new ClassPathResource("application-ai.yaml").getInputStream(), com.isthisalis.ailib.api.Configuration.class); 
    }

}
