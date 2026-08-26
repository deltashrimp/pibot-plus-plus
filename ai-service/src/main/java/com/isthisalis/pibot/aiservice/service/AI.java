package com.isthisalis.pibot.aiservice.service;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import com.isthisalis.ailib.api.Configuration;
import com.isthisalis.ailib.implementation.AiCaller;
import com.isthisalis.pibot.aiservice.config.Config;

import lombok.Getter;
import lombok.Setter;

/**
 * Placeholder AI service.
 *
 * <p>TODO: implement real LLM calls (Groq / OpenRouter) with tool calling,
 * request/usage accounting, and Core configuration reload.
 * @author IsThisALis
 */
@Service
public class AI {

    private @Autowired @Setter @Getter Configuration config;
    private AiCaller ai;


    public AI(Configuration config) {
        this.config = config;
        this.ai = new AiCaller(config, null);
    }


    /**
     * Sends message to AI model through AiLib
     * @param message Message to AI model.
     * @return AI response to message.
     * @see com.isthisalis.ailib.implementation.AiCaller
     */
    public String generate(String message) {
        return ai.ask(message);
    }

    /** No-op placeholder; providers will be (re)loaded from Core later. */
    public void reloadConfig() {
            config = Config.reload();
            System.out.println(config.toString());
        this.ai = new AiCaller(config, null);
    }
}
