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

## Configuration
Configuration can be applied via the config file and environment variables which have a higher priority. For convenience, all config file variables are lowercased while envars are uppercased. Setting `some_var` in the config file to "foo" and setting the envar `SOME_VAR` to "bar" will evaluate to "bar".