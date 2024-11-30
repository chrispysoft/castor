FROM debian:12-slim AS builder

# Set environment variables for non-interactive apt installs
ENV DEBIAN_FRONTEND=noninteractive

# Install necessary tools for building, including FFmpeg libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
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

# Set working directory
WORKDIR /app

# Copy project files into the image
COPY . /app

# Use a temporary directory for building the project
RUN mkdir /build && cd /build && \
    cmake /app && \
    cmake --build . --target lap && \
    mv lap /app/

# Create a minimal runtime image
FROM debian:12-slim

# Set environment variables for non-interactive apt installs
ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies for FFmpeg and PortAudio
RUN apt-get update && apt-get install -y --no-install-recommends \
    libasound2 \
    portaudio19-dev \
    ffmpeg \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libavfilter-dev \
    libswscale-dev \
    libswresample-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Copy the built binary from the builder stage
COPY --from=builder /app/lap /usr/local/bin/lap

# Set the default command to run the binary
CMD ["lap"]
