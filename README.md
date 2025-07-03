# MP4/MKV OpenGL Video + XAudio2 Audio Player

A high-performance MP4/MKV and others! video player written in C++ using:

- **OpenGL** (via GLFW + GLEW) for rendering H.264 video frames
- **XAudio2** for playback of AAC audio (resampled as needed)
- **FFmpeg** for decoding and format handling

Supports:
- Constant and variable framerate (VFR) H.264 video
- AAC audio streams (LC profile)
- Wall-clock and audio-clock based video pacing
- Windows desktop builds with Visual Studio

Note -- I looked everywhere for an easy example of how to play mkv and mp4 videos in 
an OpenGL window, and there just aren't that many examples, or else I suck at google. 

I know OpenGL is depricated at this point in time, feel free to replace the few lines of 
OpenGL code with 800 lines of Vulcan to do the same thing, cause I know you really want to!

This code was created with the assistance of ChatGPT, and when I say assistance I mean that 
very broadly. This is a very simple example, all I wanted to do was embed arcade gameplay videos
in my emulator front end like all the cool kids. (https://www.progettosnaps.net/index.php).
Have fun and hopefully make it better. Everything is included, load and build. 


---


## 📦 Dependencies

This project uses [vcpkg](https://github.com/microsoft/vcpkg) for dependency management:

```bash
vcpkg install ffmpeg[avcodec,avformat,swscale,swresample] glew glfw3
```

Alternatively, you can use the **official FFmpeg development builds** from:

👉 https://www.gyan.dev/ffmpeg/builds/

Download the latest "dev" version (shared or static) and link it manually in your Visual Studio project.

---

## 🛠️ Building

Open `mp4_av_player.sln` in Visual Studio and build in Release x64 mode. Make sure the following libraries are linked:

- `avformat.lib`
- `avcodec.lib`
- `avutil.lib`
- `swscale.lib`
- `swresample.lib`
- `glew32.lib`
- `glfw3dll.lib`
- `opengl32.lib`
- `xaudio2.lib`

---

## ▶️ Usage

```
mp4_av_player.exe video.mp4
```

The window will open with video playback. Press **ESC** or close the window to quit.

---

## 🔍 Notes

- Audio and video sync is preserved using timestamps (PTS) from the video stream.
- If audio is present, the audio clock is used to pace video. Otherwise, wall clock pacing is used.
- Frame timing uses high-resolution timers to handle VFR correctly.
- No GUI, menu, or transport controls (by design — this is a minimal reference player).

---

## 🧾 License

This project is released under the **Unlicense**. See [`LICENSE`](LICENSE) for details.
