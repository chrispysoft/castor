# Castor

Castor is a broadcasting playout engine written in C++ and conforming to the AURA API.

## Features

- Fetches program and playlists from `AURA` `steering` and `tank` (alpha5)
- Posts playlog, health status and clock info to `engine-api` (alpha5)
- Parses M3U files and fetches missing metadata
- 2 generic File-/Stream Players
- 1 Stereo Line Input
- 1 Stereo Line Output
- Recorder
- Stream Output
- Crossfading
- Silence Detector
- Fallback
- Configuration using file or environment variables
- Logs to console and file
- Runs natively on Debian, Ubuntu, macOS and Docker (Debian)
- Can be integrated into `AURA-playout` at ease

## Configuration
Configuration can be applied via the config file and environment variables which have a higher priority. For convenience, all config file variables are lowercased while envars are uppercased. Setting `some_var` in the config file to "foo" and setting the envar `SOME_VAR` to "bar" will evaluate to "bar". See `config/config.txt` and `.env` for examples.

## Prerequisites

Make sure your machine meets the following requirements:

- Debian 12, Ubuntu 23.10 or newer, macOS 12.0
- `git`, `cmake`
- [libcurl](https://curl.se/)
- [PortAudio](https://www.portaudio.com/)
- [FFmpeg](https://www.ffmpeg.org/)
- optional: [Docker](https://www.docker.com/)

Install dependencies on Debian/Ubuntu:
```
apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libcurl4-openssl-dev \
    libasound2-dev \
    portaudio19-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswresample-dev
```

## Building and running
### On the host
Execute `run.sh` to build and run the target.

### Docker
Run `docker compose build && docker compose up`