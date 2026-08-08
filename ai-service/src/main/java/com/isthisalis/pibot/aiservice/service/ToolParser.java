package com.isthisalis.pibot.aiservice.service;

import java.util.List;

import com.isthisalis.ailib.api.ai.tools.ToolCallParser;
import com.isthisalis.ailib.util.DTO.ToolCall;
import com.isthisalis.ailib.util.DTO.request.Message;

/**
 * Tool parser service.
 * 
 * <p>TODO: implement tools and tool parsing</p>
 */
public class ToolParser implements ToolCallParser {
    
    @Override
    public Message parseToolCalls(List<ToolCall> messages) {
        return null;
    }


    @Override
    public Message parseToolCalls(ToolCall toolCall) {
        return null;
    }
}
