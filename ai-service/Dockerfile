FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
    && rm -rf /var/lib/apt/lists/*

FROM gradle:8-jdk21 AS build
WORKDIR /app

COPY src ./src
COPY build.gradle .

RUN gradle wrapper
RUN ./gradlew dependencies
RUN ./gradlew build 


FROM eclipse-temurin:21-jre-alpine

COPY --from=build /app/build/libs/*.jar app.jar

ENV AI_PORT=8082 \
    LOG_LEVEL=info

EXPOSE 8082

ENTRYPOINT ["java", "-jar", "app.jar", "--server-port=${port: -8082}"]
