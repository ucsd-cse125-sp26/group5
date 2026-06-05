#include "client/video_player.h"

#include <cstdio>

#include "shared/util.h"

#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

namespace {
// videoId -> path under assets/videos/. Add entries here as clips are authored.
constexpr const char* kVideoPaths[] = {
    "assets/videos/intro.mpg",   // VideoId::Intro  — menu background
    "assets/videos/intro2.mpg",  // VideoId::Intro2 — connect cutscene
    "assets/videos/exit.mpg",    // VideoId::Exit   — end scene
};

void uploadPlane(GLuint tex, const plm_plane_t& p, bool allocate) {
  glBindTexture(GL_TEXTURE_2D, tex);
  if (allocate) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, p.width, p.height, 0, GL_RED,
                 GL_UNSIGNED_BYTE, p.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p.width, p.height, GL_RED,
                    GL_UNSIGNED_BYTE, p.data);
  }
}
}  // namespace

VideoPlayer::~VideoPlayer() {
  releaseGl();
  if (plm_) plm_destroy(plm_);
}

void VideoPlayer::releaseGl() {
  if (texY_) glDeleteTextures(1, &texY_);
  if (texCb_) glDeleteTextures(1, &texCb_);
  if (texCr_) glDeleteTextures(1, &texCr_);
  texY_ = texCb_ = texCr_ = 0;
  allocated_ = false;
}

bool VideoPlayer::open(const std::string& absPath, bool loop) {
  releaseGl();
  if (plm_) {
    plm_destroy(plm_);
    plm_ = nullptr;
  }
  playing_ = false;

  plm_ = plm_create_with_filename(absPath.c_str());
  if (!plm_) {
    fprintf(stderr, "VideoPlayer: could not open '%s'\n", absPath.c_str());
    return false;
  }
  plm_set_audio_enabled(plm_, 0);  // silent — skip MP2 decode entirely
  plm_set_loop(plm_, loop ? 1 : 0);
  looping_ = loop;
  dispW_ = plm_get_width(plm_);
  dispH_ = plm_get_height(plm_);
  const double fps = plm_get_framerate(plm_);
  frameTime_ = (fps > 0.0) ? 1.0 / fps : 1.0 / 30.0;
  timeAccum_ = frameTime_;  // decode the first frame on the next update()

  glGenTextures(1, &texY_);
  glGenTextures(1, &texCb_);
  glGenTextures(1, &texCr_);
  playing_ = true;
  return true;
}

void VideoPlayer::update(double dt) {
  if (!plm_ || !playing_) return;
  if (frozen_) return;  // held on the last frame; keep it on the textures
  if (dt < 0.0) dt = 0.0;
  if (dt > 0.25) dt = 0.25;  // clamp hitches so we never spiral on catch-up
  timeAccum_ += dt;

  plm_frame_t* frame = nullptr;
  int guard = 0;
  while (timeAccum_ >= frameTime_ && guard++ < 4) {
    plm_frame_t* f = plm_decode_video(plm_);
    if (!f) {
      if (looping_) {
        plm_rewind(plm_);
        f = plm_decode_video(plm_);
      } else if (freezeAtEnd_) {
        // Hold on the last decoded frame: stop advancing but stay "playing" so
        // drawFullscreenVideo keeps presenting the textures we already have.
        frozen_ = true;
        break;
      } else if (loopTailSeconds_ > 0.0) {
        // Reached the end of a play-once clip with tail-looping enabled: jump
        // back to the final `loopTailSeconds_` and keep going. plm_seek_frame
        // returns the intra frame at that point and leaves the decoder
        // positioned to continue forward (it re-loops here when it ends again).
        double tailStart = plm_get_duration(plm_) - loopTailSeconds_;
        if (tailStart < 0.0) tailStart = 0.0;
        f = plm_seek_frame(plm_, tailStart, /*seek_exact=*/0);
      }
      if (!f) {
        playing_ = false;
        break;
      }
    }
    frame = f;
    timeAccum_ -= frameTime_;
  }
  if (!frame) return;

  const bool allocate = !allocated_;
  if (allocate) {
    dispW_ = static_cast<int>(frame->width);
    dispH_ = static_cast<int>(frame->height);
    texScaleX_ = frame->y.width
                     ? static_cast<float>(frame->width) / frame->y.width
                     : 1.0f;
    texScaleY_ = frame->y.height
                     ? static_cast<float>(frame->height) / frame->y.height
                     : 1.0f;
  }

  GLint prevAlign = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  uploadPlane(texY_, frame->y, allocate);
  uploadPlane(texCb_, frame->cb, allocate);
  uploadPlane(texCr_, frame->cr, allocate);
  glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
  glBindTexture(GL_TEXTURE_2D, 0);
  allocated_ = true;
}

void VideoPlayer::bindPlanes(int unitY, int unitCb, int unitCr) const {
  glActiveTexture(GL_TEXTURE0 + unitY);
  glBindTexture(GL_TEXTURE_2D, texY_);
  glActiveTexture(GL_TEXTURE0 + unitCb);
  glBindTexture(GL_TEXTURE_2D, texCb_);
  glActiveTexture(GL_TEXTURE0 + unitCr);
  glBindTexture(GL_TEXTURE_2D, texCr_);
}

float VideoPlayer::aspect() const {
  return (dispH_ > 0) ? static_cast<float>(dispW_) / static_cast<float>(dispH_)
                      : 16.0f / 9.0f;
}

std::string videoPathFor(uint16_t id) {
  if (id >= sizeof(kVideoPaths) / sizeof(kVideoPaths[0])) return {};
  return (exeDir() / kVideoPaths[id]).string();
}
