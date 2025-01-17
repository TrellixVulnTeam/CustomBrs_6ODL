// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRI_DRI_SURFACE_H_
#define UI_OZONE_PLATFORM_DRI_DRI_SURFACE_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/skia_util.h"
#include "ui/ozone/ozone_export.h"

class SkCanvas;

namespace ui {

class DriBuffer;
class DriWrapper;

// DriSurface is used to represent a surface that can be scanned out
// to a monitor. It will store the internal state associated with the drawing
// surface associated with it. DriSurface also performs all the needed
// operations to initialize and update the drawing surface.
//
// The implementation uses dumb buffers, which is used for software rendering.
// The intent is to have one DriSurface implementation for a
// HardwareDisplayController.
//
// DoubleBufferedSurface is intended to be the software analog to
// EGLNativeSurface while DriSurface is intended to provide the glue
// necessary to initialize and display the surface to the screen.
//
// The typical usage pattern is:
// -----------------------------------------------------------------------------
// HardwareDisplayController controller;
// // Initialize controller
//
// DriSurface* surface = new DriSurface(dri_wrapper, size);
// surface.Initialize();
// controller.BindSurfaceToController(surface);
//
// while (true) {
//   SkCanvas* canvas = surface->GetDrawableForWidget();
//   DrawStuff(canvas);
//   controller.SchedulePageFlip();
//
//   Wait for page flip event. The DRM page flip handler will call
//   surface.SwapBuffers();
// }
//
// delete surface;
// -----------------------------------------------------------------------------
// In the above example the wait consists of reading a DRM pageflip event from
// the graphics card file descriptor. This is done by calling |drmHandleEvent|,
// which will read and process the event. |drmHandleEvent| will call a callback
// registered by |SchedulePageFlip| which will update the internal state.
//
// |SchedulePageFlip| can also be used to limit drawing to the screen's vsync
// since page flips only happen on vsync. In a threaded environment a message
// loop would listen on the graphics card file descriptor for an event and
// |drmHandleEvent| would be called from the message loop. The event handler
// would also be responsible for updating the renderer's state and signal that
// it is OK to start drawing the next frame.
//
// The following example will illustrate the system state transitions in one
// iteration of the above loop.
//
// 1. Both buffers contain the same image with b[0] being the front buffer
// (star will represent the frontbuffer).
// -------  -------
// |     |  |     |
// |     |  |     |
// |     |  |     |
// |     |  |     |
// -------  -------
// b[0]*    b[1]
//
// 2. Call |GetBackbuffer| to get a SkCanvas wrapper for the backbuffer and draw
// to it.
// -------  -------
// |     |  |     |
// |     |  |  d  |
// |     |  |     |
// |     |  |     |
// -------  -------
// b[0]*    b[1]
//
// 3. Call |SchedulePageFlip| to display the backbuffer. At this point we can't
// modify b[0] because it is the frontbuffer and we can't modify b[1] since it
// has been scheduled for pageflip. If we do draw in b[1] it is possible that
// the pageflip and draw happen at the same time and we could get tearing.
//
// 4. The pageflip callback is called which will call |SwapSurfaces|. Before
// |SwapSurfaces| is called the state is as following from the hardware's
// perspective:
// -------  -------
// |     |  |     |
// |     |  |  d  |
// |     |  |     |
// |     |  |     |
// -------  -------
// b[0]     b[1]*
//
// 5. |SwapSurfaces| will update out internal reference to the front buffer and
// synchronize the damaged area such that both buffers are identical. The
// damaged area is used from the SkCanvas clip.
// -------  -------
// |     |  |     |
// |  d  |  |  d  |
// |     |  |     |
// |     |  |     |
// -------  -------
// b[0]     b[1]*
//
// The synchronization consists of copying the damaged area from the frontbuffer
// to the backbuffer.
//
// At this point we're back to step 1 and can start a new draw iteration.
class OZONE_EXPORT DriSurface {
 public:
  DriSurface(DriWrapper* dri, const gfx::Size& size);
  virtual ~DriSurface();

  // Used to allocate all necessary buffers for this surface. If the
  // initialization succeeds, the device is ready to be used for drawing
  // operations.
  // Returns true if the initialization is successful, false otherwise.
  bool Initialize();

  // Returns the ID of the current backbuffer.
  uint32_t GetFramebufferId() const;

  // Returns the handle for the current backbuffer.
  uint32_t GetHandle() const;

  // Synchronizes and swaps the back buffer with the front buffer.
  void SwapBuffers();

  // Get a Skia canvas for a backbuffer.
  SkCanvas* GetDrawableForWidget();

  const gfx::Size& size() const { return size_; }

 private:
  DriBuffer* frontbuffer() const { return bitmaps_[front_buffer_].get(); }
  DriBuffer* backbuffer() const { return bitmaps_[front_buffer_ ^ 1].get(); }

  // Used to create the backing buffers.
  virtual DriBuffer* CreateBuffer();

  // Stores the connection to the graphics card. Pointer not owned by this
  // class.
  DriWrapper* dri_;

  // The actual buffers used for painting.
  scoped_ptr<DriBuffer> bitmaps_[2];

  // Keeps track of which bitmap is |buffers_| is the frontbuffer.
  int front_buffer_;

  // Surface size.
  gfx::Size size_;

  DISALLOW_COPY_AND_ASSIGN(DriSurface);
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRI_DRI_SURFACE_H_
