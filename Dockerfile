# Build image
FROM debian:12-slim AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    curl \
    libcurl4-openssl-dev \
    libasound2-dev \
    portaudio19-dev \
    ffmpeg \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libavfilter-dev \
    libswscale-dev \
    libswresample-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY src/ src/
COPY CMakeLists.txt .
RUN mkdir /build && cd /build && \
    cmake /app && \
    cmake --build . --target lap

# Runtime image
FROM debian:12-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    libasound2 \
    libportaudio2 \
    ffmpeg \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
COPY --from=builder /build/lap /usr/local/bin/lap
CMD ["lap"]
