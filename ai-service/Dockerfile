# Syntax=docker/dockerfile:1

# ---- ai-service: FastAPI based AI/LLM service ----
FROM python:3.11-slim

WORKDIR /app

# Install Python dependencies first to leverage Docker layer caching
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

# Copy application code
COPY app/ ./app/

# AI service configuration
ENV AI_PORT=8082

EXPOSE 8082

HEALTHCHECK CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:8082/health')"

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8082"]
