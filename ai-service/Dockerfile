# ---- Stage 1: builder ----
# Compiles the Spring Boot application with Maven.
FROM maven:3.9-eclipse-temurin-17 AS builder

WORKDIR /app
COPY pom.xml .
# Resolve dependencies into the Maven cache first so source changes do not
# re-download the world.
RUN mvn -B -q -DskipTests dependency:go-offline

COPY src ./src
RUN mvn -B -q -DskipTests package

# ---- Stage 2: runtime ----
# Minimal JRE image; curl is needed by the Docker healthcheck.
FROM eclipse-temurin:17-jre AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/target/ai-service.jar /app/ai-service.jar

ENV AI_PORT=8082 \
    LOG_LEVEL=info

EXPOSE 8082

ENTRYPOINT ["java", "-jar", "/app/ai-service.jar"]
