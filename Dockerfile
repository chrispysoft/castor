# Build image
FROM debian:12-slim AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libcurl4-openssl-dev \
    libasound2-dev \
    portaudio19-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswresample-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY src/ src/
COPY include/ include/
COPY CMakeLists.txt .
RUN mkdir /build && cd /build && \
    cmake /app && \
    cmake --build . --target castor && \
    strip castor

# Runtime image
FROM debian:12-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    ca-certificates \
    libasound2 \
    libportaudio2 \
    ffmpeg \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /build/castor /usr/local/bin/castor
COPY parameters.json /app/parameters.json
COPY www /app/www
CMD ["castor"]
