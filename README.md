# Castor - C++ Broadcasting Playout Engine

**Castor** is a high-performance, C++-based broadcasting playout engine optimized for speed, high availability, and seamless integration with the AURA API.

## Features

- Fetches program und media data from **AURA steering API**  (alpha6)
- Posts playlogs, status and clock info to **AURA engine API** (alpha6)
- Parses M3U playlists and retrieves missing metadata
- 1 stereo line input and 1 stereo line output
- Recorder functionality
- Stream output support
- Silence detection
- Fallback mechanism
- Fade in/out (regular program)
- Crossfading (fallback only)
- Native support for **Debian**, **Ubuntu**, **macOS**, and **Docker** (Debian-based)

## Getting Started

### Running with Docker
Castor is available as a prebuilt Docker image (amd64). You can either pull a prebuilt image from [Docker Hub](https://hub.docker.com/repository/docker/crispybitsapp/castor/general) or build from source.

### Building and Running from Source

Ensure your system meets the following requirements:

- **Debian 12**, **Ubuntu 23.10** (or newer), **macOS 10.12** (or newer)
- Tools: `cmake`
- Libraries:
  - [libcurl](https://curl.se/)
  - [PortAudio](https://www.portaudio.com/)
  - [FFmpeg](https://www.ffmpeg.org/)
- Optional: [Docker](https://www.docker.com/)

To install dependencies on Debian/Ubuntu:

```bash
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

To build and run, execute:

```bash
make run
```

For more build options (e.g., clean, help), simply run `make`.

### Building and Running from Source with Docker

To build and run with Docker, execute:

```bash
docker compose build && docker compose up
```

## Configuration

Castor can be configured using a configuration file or environment variables. Environment variables have a higher priority than the config file. For convenience, all config file variables are lowercased, while environment variables are uppercased. For example, if `some_var` is set to "foo" in the config file and `SOME_VAR` is set to "bar" in the environment, the evaluated value will be "bar".

Refer to `config/config.txt` and `.env` for example configurations.

## Control Logic

### Calendar

The **Calendar** component tracks the current program by periodically querying the API and comparing results (based on the `calendar_update_interval`). It notifies the **Scheduler** of any changes, but only if the queried item differs from the previous one. Items are considered equal if their `start`, `end`, and `uri` values match. The notification callback provides a reference to an array of `PlayItem` pointers.

Note: M3U playlists are converted into `PlayItem` objects, even if some metadata is missing (which is needed for calculating durations). If metadata is missing, the **CodecReader** is used to retrieve the duration of each playlist entry. TODO: implement **M3UPlayer**, which loads all files into a single buffer; maybe with fade-zones by summing both tracks...

### Scheduler and Player

The **Scheduler** listens for changes in the Calendar and spawns a **Player** for each `PlayItem`. A `PlayItem` contains the `start` and `end` time, as well as the `uri`. Players can be scheduled automatically or manually through control functions like `load`, `start`, etc. (refer to **Fallback** for more details).

The **PlayerFactory** determines the appropriate player subclass and may serve as a memory manager in future versions (recycling and reusing player buffers).

Player types include:

- **FilePlayer**: Loads and plays audio files
- **StreamPlayer**: Plays live streams with temporary buffering
- **LinePlayer**: Handles audio interface input
- **M3UPlayer** (TODO): Reduces the number of **FilePlayer** instances

Each **Player** triggers an `onPlay` callback to inform the **Scheduler** of changes in the current track. Program changes are detected through further checks. These events control the **Recorder**, update stream metadata, and post changes to the API. (TODO: also listen for stop events).

## Signal Processing Chain

Each rendering cycle begins by requesting the frontmost player (assumed to be active) to render a frame (block of N samples) to the output buffer. This buffer serves as the input buffer for the **Recorder** (if active) and the **SilenceDetector**.

The **SilenceDetector** analyzes the RMS value of the buffer on a background thread and triggers a callback if silence changes according to the specified durations, activating or deactivating **Fallback**.

If **Fallback** is active, the output buffer becomes the render target. The buffer is then passed to **StreamOutput** (if enabled) and finally to the **AudioClient**, which interfaces with the audio hardware.

### Fallback
At startup, audio files located in `audio_fallback_path` (including those referenced in m3u playlists) are cached. The maximum duration of cached content is controlled by `preload_time_fallback` and depends on the sample rate and available RAM (which may be lower in a Docker environment than on the host system). The fallback queue reloads automatically once all tracks have been played. Additionally, fallback playback supports "true crossfading" by overlapping two tracks during the transition window and applying smooth, exponential fade curves.

### Recorder
To enable automatic recording, set `audio_record_path` to a valid directory. Each change of the current show starts a new and stops the previous recording.

### Stream Output
To enable, set `stream_out_url` to a valid icecast url. Retry interval on errors: 5 sec.

## Known Issues

- Fallback stops and starts at the same track position without fading in/out
- Static fade in/out times may not be suitable for all show types

## Sources

Castor is built to work with [AURA](https://aura.radio/). These open-source libraries are used to handle media processing, networking, and audio management:

- [libcurl](https://curl.se/) – for network communications
- [PortAudio](https://www.portaudio.com/) – for audio I/O
- [FFmpeg](https://www.ffmpeg.org/) – for multimedia file handling
- [nlohmann/json](https://github.com/nlohmann/json) – a header-only JSON library for data serialization

The scheduling logic in Castor is based on an existing Python implementation of **AURA-engine**, which served as the foundation for the design of the current C++ version.

We would like to acknowledge and thank the authors and contributors of the AURA Project and these libraries for their invaluable work in making Castor possible.