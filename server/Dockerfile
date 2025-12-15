FROM ubuntu:22.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git libboost-all-dev libssl-dev && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . /app
RUN cmake -S . -B build && cmake --build build --target server_app

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-all-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /srv
COPY --from=build /app/build/server_app /srv/server_app
EXPOSE 8080
CMD ["/srv/server_app"]
