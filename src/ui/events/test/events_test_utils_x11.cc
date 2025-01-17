// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/test/events_test_utils_x11.h"

#include <X11/extensions/XI2.h>
#include <X11/keysym.h>
#include <X11/X.h>
#include <X11/Xlib.h>

#include "base/logging.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_code_conversion_x.h"
#include "ui/events/x/touch_factory_x11.h"

namespace {

// Converts ui::EventType to state for X*Events.
unsigned int XEventState(int flags) {
  return
      ((flags & ui::EF_SHIFT_DOWN) ? ShiftMask : 0) |
      ((flags & ui::EF_CONTROL_DOWN) ? ControlMask : 0) |
      ((flags & ui::EF_ALT_DOWN) ? Mod1Mask : 0) |
      ((flags & ui::EF_CAPS_LOCK_DOWN) ? LockMask : 0) |
      ((flags & ui::EF_LEFT_MOUSE_BUTTON) ? Button1Mask: 0) |
      ((flags & ui::EF_MIDDLE_MOUSE_BUTTON) ? Button2Mask: 0) |
      ((flags & ui::EF_RIGHT_MOUSE_BUTTON) ? Button3Mask: 0);
}

// Converts EventType to XKeyEvent type.
int XKeyEventType(ui::EventType type) {
  switch (type) {
    case ui::ET_KEY_PRESSED:
      return KeyPress;
    case ui::ET_KEY_RELEASED:
      return KeyRelease;
    default:
      return 0;
  }
}

int XIButtonEventType(ui::EventType type) {
  switch (type) {
    case ui::ET_MOUSEWHEEL:
    case ui::ET_MOUSE_PRESSED:
      // The button release X events for mouse wheels are dropped by Aura.
      return XI_ButtonPress;
    case ui::ET_MOUSE_RELEASED:
      return XI_ButtonRelease;
    default:
      NOTREACHED();
      return 0;
  }
}

// Converts KeyboardCode to XKeyEvent keycode.
unsigned int XKeyEventKeyCode(ui::KeyboardCode key_code,
                              int flags,
                              XDisplay* display) {
  const int keysym = XKeysymForWindowsKeyCode(key_code,
                                              flags & ui::EF_SHIFT_DOWN);
  // Tests assume the keycode for XK_less is equal to the one of XK_comma,
  // but XKeysymToKeycode returns 94 for XK_less while it returns 59 for
  // XK_comma. Here we convert the value for XK_less to the value for XK_comma.
  return (keysym == XK_less) ? 59 : XKeysymToKeycode(display, keysym);
}

// Converts Aura event type and flag to X button event.
unsigned int XButtonEventButton(ui::EventType type,
                                int flags) {
  // Aura events don't keep track of mouse wheel button, so just return
  // the first mouse wheel button.
  if (type == ui::ET_MOUSEWHEEL)
    return Button4;

  if (flags & ui::EF_LEFT_MOUSE_BUTTON)
    return Button1;
  if (flags & ui::EF_MIDDLE_MOUSE_BUTTON)
    return Button2;
  if (flags & ui::EF_RIGHT_MOUSE_BUTTON)
    return Button3;

  return 0;
}

void InitValuatorsForXIDeviceEvent(XIDeviceEvent* xiev) {
  int valuator_count = ui::DeviceDataManager::DT_LAST_ENTRY;
  xiev->valuators.mask_len = (valuator_count / 8) + 1;
  xiev->valuators.mask = new unsigned char[xiev->valuators.mask_len];
  memset(xiev->valuators.mask, 0, xiev->valuators.mask_len);
  xiev->valuators.values = new double[valuator_count];
}

XEvent* CreateXInput2Event(int deviceid,
                           int evtype,
                           int tracking_id,
                           const gfx::Point& location) {
  XEvent* event = new XEvent;
  memset(event, 0, sizeof(*event));
  event->type = GenericEvent;
  event->xcookie.data = new XIDeviceEvent;
  XIDeviceEvent* xiev =
      static_cast<XIDeviceEvent*>(event->xcookie.data);
  memset(xiev, 0, sizeof(XIDeviceEvent));
  xiev->deviceid = deviceid;
  xiev->sourceid = deviceid;
  xiev->evtype = evtype;
  xiev->detail = tracking_id;
  xiev->event_x = location.x();
  xiev->event_y = location.y();
  xiev->event = DefaultRootWindow(gfx::GetXDisplay());
  if (evtype == XI_ButtonPress || evtype == XI_ButtonRelease) {
    xiev->buttons.mask_len = 8;
    xiev->buttons.mask = new unsigned char[xiev->buttons.mask_len];
    memset(xiev->buttons.mask, 0, xiev->buttons.mask_len);
  }
  return event;
}

}  // namespace

namespace ui {

// XInput2 events contain additional data that need to be explicitly freed (see
// |CreateXInput2Event()|.
void XEventDeleter::operator()(XEvent* event) {
  if (event->type == GenericEvent) {
    XIDeviceEvent* xiev =
        static_cast<XIDeviceEvent*>(event->xcookie.data);
    if (xiev) {
      delete[] xiev->valuators.mask;
      delete[] xiev->valuators.values;
      delete[] xiev->buttons.mask;
      delete xiev;
    }
  }
  delete event;
}

ScopedXI2Event::ScopedXI2Event() {}
ScopedXI2Event::~ScopedXI2Event() {}

void ScopedXI2Event::InitKeyEvent(EventType type,
                                  KeyboardCode key_code,
                                  int flags) {
  XDisplay* display = gfx::GetXDisplay();
  event_.reset(new XEvent);
  memset(event_.get(), 0, sizeof(XEvent));
  event_->type = XKeyEventType(type);
  CHECK_NE(0, event_->type);
  event_->xkey.serial = 0;
  event_->xkey.send_event = 0;
  event_->xkey.display = display;
  event_->xkey.time = 0;
  event_->xkey.window = 0;
  event_->xkey.root = 0;
  event_->xkey.subwindow = 0;
  event_->xkey.x = 0;
  event_->xkey.y = 0;
  event_->xkey.x_root = 0;
  event_->xkey.y_root = 0;
  event_->xkey.state = XEventState(flags);
  event_->xkey.keycode = XKeyEventKeyCode(key_code, flags, display);
  event_->xkey.same_screen = 1;
}

void ScopedXI2Event::InitGenericButtonEvent(int deviceid,
                                            EventType type,
                                            const gfx::Point& location,
                                            int flags) {
  event_.reset(CreateXInput2Event(deviceid,
                                  XIButtonEventType(type), 0, gfx::Point()));
  XIDeviceEvent* xievent = static_cast<XIDeviceEvent*>(event_->xcookie.data);
  xievent->mods.effective = XEventState(flags);
  xievent->detail = XButtonEventButton(type, flags);
  xievent->event_x = location.x();
  xievent->event_y = location.y();
  XISetMask(xievent->buttons.mask, xievent->detail);
  // Setup an empty valuator list for generic button events.
  SetUpValuators(std::vector<Valuator>());
}

void ScopedXI2Event::InitGenericMouseWheelEvent(int deviceid,
                                                int wheel_delta,
                                                int flags) {
  InitGenericButtonEvent(deviceid, ui::ET_MOUSEWHEEL, gfx::Point(), flags);
  XIDeviceEvent* xievent = static_cast<XIDeviceEvent*>(event_->xcookie.data);
  xievent->detail = wheel_delta > 0 ? Button4 : Button5;
}

void ScopedXI2Event::InitScrollEvent(int deviceid,
                                     int x_offset,
                                     int y_offset,
                                     int x_offset_ordinal,
                                     int y_offset_ordinal,
                                     int finger_count) {
  event_.reset(CreateXInput2Event(deviceid, XI_Motion, 0, gfx::Point()));

  Valuator valuators[] = {
    Valuator(DeviceDataManager::DT_CMT_SCROLL_X, x_offset),
    Valuator(DeviceDataManager::DT_CMT_SCROLL_Y, y_offset),
    Valuator(DeviceDataManager::DT_CMT_ORDINAL_X, x_offset_ordinal),
    Valuator(DeviceDataManager::DT_CMT_ORDINAL_Y, y_offset_ordinal),
    Valuator(DeviceDataManager::DT_CMT_FINGER_COUNT, finger_count)
  };
  SetUpValuators(
      std::vector<Valuator>(valuators, valuators + arraysize(valuators)));
}

void ScopedXI2Event::InitFlingScrollEvent(int deviceid,
                                          int x_velocity,
                                          int y_velocity,
                                          int x_velocity_ordinal,
                                          int y_velocity_ordinal,
                                          bool is_cancel) {
  event_.reset(CreateXInput2Event(deviceid, XI_Motion, deviceid, gfx::Point()));

  Valuator valuators[] = {
    Valuator(DeviceDataManager::DT_CMT_FLING_STATE, is_cancel ? 1 : 0),
    Valuator(DeviceDataManager::DT_CMT_FLING_Y, y_velocity),
    Valuator(DeviceDataManager::DT_CMT_ORDINAL_Y, y_velocity_ordinal),
    Valuator(DeviceDataManager::DT_CMT_FLING_X, x_velocity),
    Valuator(DeviceDataManager::DT_CMT_ORDINAL_X, x_velocity_ordinal)
  };

  SetUpValuators(
      std::vector<Valuator>(valuators, valuators + arraysize(valuators)));
}

void ScopedXI2Event::InitTouchEvent(int deviceid,
                                    int evtype,
                                    int tracking_id,
                                    const gfx::Point& location,
                                    const std::vector<Valuator>& valuators) {
  event_.reset(CreateXInput2Event(deviceid, evtype, tracking_id, location));
  SetUpValuators(valuators);
}

void ScopedXI2Event::SetUpValuators(const std::vector<Valuator>& valuators) {
  CHECK(event_.get());
  CHECK_EQ(GenericEvent, event_->type);
  XIDeviceEvent* xiev = static_cast<XIDeviceEvent*>(event_->xcookie.data);
  InitValuatorsForXIDeviceEvent(xiev);
  ui::DeviceDataManager* manager = ui::DeviceDataManager::GetInstance();
  for (size_t i = 0; i < valuators.size(); ++i) {
    manager->SetValuatorDataForTest(xiev, valuators[i].data_type,
                                    valuators[i].value);
  }
}

void SetUpTouchPadForTest(unsigned int deviceid) {
  std::vector<unsigned int> device_list;
  device_list.push_back(deviceid);

  TouchFactory::GetInstance()->SetPointerDeviceForTest(device_list);
  ui::DeviceDataManager* manager = ui::DeviceDataManager::GetInstance();
  manager->SetDeviceListForTest(std::vector<unsigned int>(), device_list);
}

void SetUpTouchDevicesForTest(const std::vector<unsigned int>& devices) {
  TouchFactory::GetInstance()->SetTouchDeviceForTest(devices);
  ui::DeviceDataManager* manager = ui::DeviceDataManager::GetInstance();
  manager->SetDeviceListForTest(devices, std::vector<unsigned int>());
}

}  // namespace ui
