FROM gradle:8-jdk21 AS build
WORKDIR /app

COPY src ./src
COPY build.gradle .

RUN gradle wrapper
RUN ./gradlew dependencies
RUN ./gradlew build 


FROM eclipse-temurin:21-jre-alpine

RUN apk add --no-cache curl

COPY --from=build /app/build/libs/*.jar app.jar

ENV AI_PORT=8082 \
    LOG_LEVEL=info

EXPOSE 8082

ENTRYPOINT ["java", "-jar", "app.jar"]
