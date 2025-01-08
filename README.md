# Castor

Castor is a broadcasting playout engine written in C++ and conforming to the AURA API.

## Features

- Fetches program and playlists from `AURA` `steering` and `tank` (alpha5)
- Posts playlogs, health status and clock info to `engine-api` (alpha5)
- Parses M3U files and fetches missing metadata (duration)
- 2 generic File-/Stream Players (can read chunkwise or at once)
- 1 Stereo Line Input
- 1 Stereo Line Output
- Recorder
- Stream Output (experimantal)
- Crossfading
- Silence Detector
- Fallback (playlist, stream, test signal)
- Configuration using file or environment variables
- Logs to console and file
- Runs natively on Debian, Ubuntu, macOS and Docker (Debian)
- Can be integrated into `AURA-playout` at ease


### Fast playback availibilty 
To start the playback as quick as possible, sources are read into a fifo buffer with a default capacity of 1 hour (44100 kHz sample rate). The playback can start as soon as enough samples have been received, converted and stored in the buffer. This ensures a minimal downtime after restart. If the buffer has raeached its capacity, the read loop pauses and continues if the player has pulled the next block to render. This applies also to live streams, where the buffer acts more like a ring buffer since the received audio data is in realtime.

## Configuration
Configuration can be applied via the config file and environment variables which have a higher priority. For convenience, all config file variables are lowercased while envars are uppercased. Setting `some_var` in the config file to "foo" and setting the envar `SOME_VAR` to "bar" will evaluate to "bar".