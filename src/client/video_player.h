#pragma once

#include <glad/gl.h>

#include <cstdint>
#include <string>

// Forward-declared so this header (pulled in widely via client_graphics.h) does
// not drag the pl_mpeg implementation/declarations into every TU. Only
// video_player.cpp includes pl_mpeg.h (and defines PL_MPEG_IMPLEMENTATION).
struct plm_t;

// Decodes a silent MPEG-1 (.mpg) into three single-channel GL_R8 textures
// (planar YUV420); a shader converts YUV->RGB. RENDER/GL THREAD ONLY — every
// method touches the GL context or the (non-thread-safe) decoder.
class VideoPlayer {
 public:
  VideoPlayer() = default;
  ~VideoPlayer();
  VideoPlayer(const VideoPlayer&) = delete;
  VideoPlayer& operator=(const VideoPlayer&) = delete;

  // Returns false on a missing/invalid file; the caller treats that as a no-op.
  bool open(const std::string& absPath, bool loop);
  void update(double dt);  // decodes + uploads the frame(s) for elapsed dt
  void bindPlanes(int unitY, int unitCb, int unitCr) const;
  void stop() { playing_ = false; }

  bool isPlaying() const { return playing_; }
  // Display aspect ratio (w/h) for letterboxing.
  float aspect() const;
  // Visible fraction of each plane texture (planes are padded up to a 16px
  // macroblock, so the right/bottom edge is garbage and must be cropped).
  float texScaleX() const { return texScaleX_; }
  float texScaleY() const { return texScaleY_; }

 private:
  void releaseGl();

  plm_t* plm_ = nullptr;
  GLuint texY_ = 0, texCb_ = 0, texCr_ = 0;
  int dispW_ = 0, dispH_ = 0;
  float texScaleX_ = 1.0f, texScaleY_ = 1.0f;
  double frameTime_ = 1.0 / 30.0;  // seconds per frame (1 / framerate)
  double timeAccum_ = 0.0;
  bool playing_ = false;
  bool looping_ = false;
  bool allocated_ = false;
};

// Maps a wire videoId to an absolute path under assets/videos/ (resolved via
// exeDir()). Returns "" for an out-of-range id.
std::string videoPathFor(uint16_t id);
