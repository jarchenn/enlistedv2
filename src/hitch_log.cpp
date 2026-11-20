// CP step 0: the always-on hitch log (hitch_log.h; corpus 01 s8.1 item 5: a frame over 1.5x the moving median dumps
// what happened that frame).
#include "hitch_log.h"
#include "common.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <vector>

namespace {
HitchFrame gCur, gLast;
long long gHitches = 0;
std::vector<double> gRing; size_t gRi = 0;
double gPrevEnd = 0, gLastLog = 0; int gSuppressed = 0;

int chans(GLenum f) { return f == GL_RED || f == GL_RED_INTEGER || f == GL_DEPTH_COMPONENT ? 1 : f == GL_RG ? 2 : f == GL_RGB || f == GL_BGR ? 3 : 4; }
int typeBytes(GLenum t) { return t == GL_FLOAT || t == GL_UNSIGNED_INT || t == GL_INT ? 4 : t == GL_HALF_FLOAT || t == GL_UNSIGNED_SHORT || t == GL_SHORT ? 2 : 1; }
double mb(double bytes) { return bytes / (1024.0 * 1024.0); }

PFNGLLINKPROGRAMPROC oLink; PFNGLTEXIMAGE2DPROC oTexImage2D; PFNGLTEXSUBIMAGE2DPROC oTexSub2D;
PFNGLTEXTURESUBIMAGE2DPROC oTextureSub2D; PFNGLTEXTURESUBIMAGE3DPROC oTextureSub3D;
PFNGLCOMPRESSEDTEXTURESUBIMAGE2DPROC oCompSub2D; PFNGLCOMPRESSEDTEXTURESUBIMAGE3DPROC oCompSub3D;
PFNGLNAMEDBUFFERDATAPROC oNBData; PFNGLNAMEDBUFFERSUBDATAPROC oNBSub; PFNGLBUFFERDATAPROC oBData; PFNGLBUFFERSUBDATAPROC oBSub;

void APIENTRY wLink(GLuint p) { ++gCur.links; oLink(p); }
void APIENTRY wTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void* px) {
  if (px) { ++gCur.texUploads; gCur.texMB += mb((double)w * h * chans(f) * typeBytes(ty)); } oTexImage2D(t, l, i, w, h, b, f, ty, px); }
void APIENTRY wTexSub2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void* px) {
  ++gCur.texUploads; gCur.texMB += mb((double)w * h * chans(f) * typeBytes(ty)); oTexSub2D(t, l, x, y, w, h, f, ty, px); }
void APIENTRY wTextureSub2D(GLuint t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void* px) {
  ++gCur.texUploads; gCur.texMB += mb((double)w * h * chans(f) * typeBytes(ty)); oTextureSub2D(t, l, x, y, w, h, f, ty, px); }
void APIENTRY wTextureSub3D(GLuint t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLenum ty, const void* px) {
  ++gCur.texUploads; gCur.texMB += mb((double)w * h * d * chans(f) * typeBytes(ty)); oTextureSub3D(t, l, x, y, z, w, h, d, f, ty, px); }
void APIENTRY wCompSub2D(GLuint t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLsizei n, const void* px) {
  ++gCur.texUploads; gCur.texMB += mb(n); oCompSub2D(t, l, x, y, w, h, f, n, px); }
void APIENTRY wCompSub3D(GLuint t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLsizei n, const void* px) {
  ++gCur.texUploads; gCur.texMB += mb(n); oCompSub3D(t, l, x, y, z, w, h, d, f, n, px); }
void APIENTRY wNBData(GLuint b, GLsizeiptr n, const void* p, GLenum u) { if (p) { ++gCur.bufUploads; gCur.bufMB += mb((double)n); } oNBData(b, n, p, u); }
void APIENTRY wNBSub(GLuint b, GLintptr o, GLsizeiptr n, const void* p) { ++gCur.bufUploads; gCur.bufMB += mb((double)n); oNBSub(b, o, n, p); }
void APIENTRY wBData(GLenum t, GLsizeiptr n, const void* p, GLenum u) { if (p) { ++gCur.bufUploads; gCur.bufMB += mb((double)n); } oBData(t, n, p, u); }
void APIENTRY wBSub(GLenum t, GLintptr o, GLsizeiptr n, const void* p) { ++gCur.bufUploads; gCur.bufMB += mb((double)n); oBSub(t, o, n, p); }
template <class F> void wrap(F& slot, F& orig, F w) { if (slot) { orig = slot; slot = w; } }
}   // namespace

void hitchInstall() {
  wrap(glad_glLinkProgram, oLink, &wLink); wrap(glad_glTexImage2D, oTexImage2D, &wTexImage2D); wrap(glad_glTexSubImage2D, oTexSub2D, &wTexSub2D);
  wrap(glad_glTextureSubImage2D, oTextureSub2D, &wTextureSub2D); wrap(glad_glTextureSubImage3D, oTextureSub3D, &wTextureSub3D);
  wrap(glad_glCompressedTextureSubImage2D, oCompSub2D, &wCompSub2D); wrap(glad_glCompressedTextureSubImage3D, oCompSub3D, &wCompSub3D);
  wrap(glad_glNamedBufferData, oNBData, &wNBData); wrap(glad_glNamedBufferSubData, oNBSub, &wNBSub);
  wrap(glad_glBufferData, oBData, &wBData); wrap(glad_glBufferSubData, oBSub, &wBSub);
  gRing.assign(121, 0.0);
}
HitchFrame& hitchCur() { return gCur; }
const HitchFrame& hitchLast() { return gLast; }
long long hitchCount() { return gHitches; }

void hitchFrameEnd(double swapMs, bool loading) {
  double now = glfwGetTime();
  gCur.swapMs = swapMs; gCur.frameMs = gPrevEnd > 0 ? (now - gPrevEnd) * 1000.0 : 0; gPrevEnd = now;
  if (loading || gRing.empty()) { std::fill(gRing.begin(), gRing.end(), 0.0); gRi = 0; }   // a loading screen restarts the median
  else if (gCur.frameMs > 0) {
    size_t n = std::min(gRi, gRing.size());
    if (n >= 60) {   // a median over at least one second of frames
      std::vector<double> v(gRing.begin(), gRing.begin() + n); std::nth_element(v.begin(), v.begin() + n / 2, v.end());
      gCur.medianMs = v[n / 2];
      gCur.hitch = gCur.frameMs > 1.5 * gCur.medianMs;
    }
    gRing[gRi % gRing.size()] = gCur.frameMs; ++gRi;
  }
  if (gCur.hitch) {
    ++gHitches;
    if (now - gLastLog >= 0.5) {   // at most two lines a second; the rest are counted
      logf("hitch: %.1f ms (median %.1f): tex uploads %d (%.1f MB), buffer uploads %d (%.1f MB), programs linked %d, flushPending %.1f ms, buildRT %d, swap %.1f ms%s",
           gCur.frameMs, gCur.medianMs, gCur.texUploads, gCur.texMB, gCur.bufUploads, gCur.bufMB, gCur.links, gCur.flushMs, gCur.buildRT, gCur.swapMs,
           gSuppressed ? (" (+" + std::to_string(gSuppressed) + " more since the last line)").c_str() : "");
      gLastLog = now; gSuppressed = 0;
    } else ++gSuppressed;
  }
  gLast = gCur; gCur = HitchFrame{};
}
