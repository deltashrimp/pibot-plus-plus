package com.isthisalis.pibot.aiservice.api;

import lombok.Value;

@Value
/**
 * Data-class representing request to this service.
 */
public class Request {
    /** 
     * Authentification header value.
     **/
    String xApiKey;

    /** 
     * Request content.
     **/
    String message;
}
