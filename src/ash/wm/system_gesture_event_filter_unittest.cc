// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/system_gesture_event_filter.h"

#include "ash/accelerators/accelerator_controller.h"
#include "ash/ash_switches.h"
#include "ash/display/display_manager.h"
#include "ash/shelf/shelf.h"
#include "ash/shelf/shelf_model.h"
#include "ash/shell.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/display_manager_test_api.h"
#include "ash/test/shell_test_api.h"
#include "ash/test/test_shelf_delegate.h"
#include "ash/wm/gestures/long_press_affordance_handler.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/aura/env.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/base/hit_test.h"
#include "ui/base/ui_base_switches.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/event_utils.h"
#include "ui/events/gestures/gesture_configuration.h"
#include "ui/events/test/test_event_handler.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/size.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/non_client_view.h"

namespace ash {
namespace test {

namespace {

class ResizableWidgetDelegate : public views::WidgetDelegateView {
 public:
  ResizableWidgetDelegate() {}
  virtual ~ResizableWidgetDelegate() {}

 private:
  virtual bool CanResize() const OVERRIDE { return true; }
  virtual bool CanMaximize() const OVERRIDE { return true; }
  virtual void DeleteDelegate() OVERRIDE { delete this; }

  DISALLOW_COPY_AND_ASSIGN(ResizableWidgetDelegate);
};

// Support class for testing windows with a maximum size.
class MaxSizeNCFV : public views::NonClientFrameView {
 public:
  MaxSizeNCFV() {}
 private:
  virtual gfx::Size GetMaximumSize() OVERRIDE {
    return gfx::Size(200, 200);
  }
  virtual gfx::Rect GetBoundsForClientView() const OVERRIDE {
    return gfx::Rect();
  };

  virtual gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const OVERRIDE {
    return gfx::Rect();
  };

  // This function must ask the ClientView to do a hittest.  We don't do this in
  // the parent NonClientView because that makes it more difficult to calculate
  // hittests for regions that are partially obscured by the ClientView, e.g.
  // HTSYSMENU.
  virtual int NonClientHitTest(const gfx::Point& point) OVERRIDE {
    return HTNOWHERE;
  }
  virtual void GetWindowMask(const gfx::Size& size,
                             gfx::Path* window_mask) OVERRIDE {}
  virtual void ResetWindowControls() OVERRIDE {}
  virtual void UpdateWindowIcon() OVERRIDE {}
  virtual void UpdateWindowTitle() OVERRIDE {}

  DISALLOW_COPY_AND_ASSIGN(MaxSizeNCFV);
};

class MaxSizeWidgetDelegate : public views::WidgetDelegateView {
 public:
  MaxSizeWidgetDelegate() {}
  virtual ~MaxSizeWidgetDelegate() {}

 private:
  virtual bool CanResize() const OVERRIDE { return true; }
  virtual bool CanMaximize() const OVERRIDE { return false; }
  virtual void DeleteDelegate() OVERRIDE { delete this; }
  virtual views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) OVERRIDE {
    return new MaxSizeNCFV;
  }

  DISALLOW_COPY_AND_ASSIGN(MaxSizeWidgetDelegate);
};

} // namespace

class SystemGestureEventFilterTest
    : public AshTestBase,
      public testing::WithParamInterface<bool> {
 public:
  SystemGestureEventFilterTest() : AshTestBase(), docked_enabled_(GetParam()) {}
  virtual ~SystemGestureEventFilterTest() {}

  LongPressAffordanceHandler* GetLongPressAffordance() {
    ShellTestApi shell_test(Shell::GetInstance());
    return shell_test.system_gesture_event_filter()->
        long_press_affordance_.get();
  }

  base::OneShotTimer<LongPressAffordanceHandler>*
  GetLongPressAffordanceTimer() {
    return &GetLongPressAffordance()->timer_;
  }

  aura::Window* GetLongPressAffordanceTarget() {
    return GetLongPressAffordance()->tap_down_target_;
  }

  views::View* GetLongPressAffordanceView() {
    return reinterpret_cast<views::View*>(
        GetLongPressAffordance()->view_.get());
  }

  // Overridden from AshTestBase:
  virtual void SetUp() OVERRIDE {
    if (!docked_enabled_) {
      CommandLine::ForCurrentProcess()->AppendSwitch(
          ash::switches::kAshDisableDockedWindows);
    }
    test::AshTestBase::SetUp();
    // Enable brightness key.
    test::DisplayManagerTestApi(Shell::GetInstance()->display_manager()).
        SetFirstDisplayAsInternalDisplay();
  }

 private:
  // true if docked windows are enabled with a flag.
  bool docked_enabled_;

  DISALLOW_COPY_AND_ASSIGN(SystemGestureEventFilterTest);
};

ui::GestureEvent* CreateGesture(ui::EventType type,
                                    int x,
                                    int y,
                                    float delta_x,
                                    float delta_y,
                                    int touch_id) {
  return new ui::GestureEvent(type, x, y, 0,
      base::TimeDelta::FromMilliseconds(base::Time::Now().ToDoubleT() * 1000),
      ui::GestureEventDetails(type, delta_x, delta_y), 1 << touch_id);
}

TEST_P(SystemGestureEventFilterTest, LongPressAffordanceStateOnCaptureLoss) {
  aura::Window* root_window = Shell::GetPrimaryRootWindow();

  aura::test::TestWindowDelegate delegate;
  scoped_ptr<aura::Window> window0(
      aura::test::CreateTestWindowWithDelegate(
          &delegate, 9, gfx::Rect(0, 0, 100, 100), root_window));
  scoped_ptr<aura::Window> window1(
      aura::test::CreateTestWindowWithDelegate(
          &delegate, 10, gfx::Rect(0, 0, 100, 50), window0.get()));
  scoped_ptr<aura::Window> window2(
      aura::test::CreateTestWindowWithDelegate(
          &delegate, 11, gfx::Rect(0, 50, 100, 50), window0.get()));

  const int kTouchId = 5;

  // Capture first window.
  window1->SetCapture();
  EXPECT_TRUE(window1->HasCapture());

  // Send touch event to first window.
  ui::TouchEvent press(ui::ET_TOUCH_PRESSED,
                       gfx::Point(10, 10),
                       kTouchId,
                       ui::EventTimeForNow());
  ui::EventDispatchDetails details =
      root_window->GetHost()->dispatcher()->OnEventFromSource(&press);
  ASSERT_FALSE(details.dispatcher_destroyed);
  EXPECT_TRUE(window1->HasCapture());

  base::OneShotTimer<LongPressAffordanceHandler>* timer =
      GetLongPressAffordanceTimer();
  EXPECT_TRUE(timer->IsRunning());
  EXPECT_EQ(window1, GetLongPressAffordanceTarget());

  // Force timeout so that the affordance animation can start.
  timer->user_task().Run();
  timer->Stop();
  EXPECT_TRUE(GetLongPressAffordance()->is_animating());

  // Change capture.
  window2->SetCapture();
  EXPECT_TRUE(window2->HasCapture());

  EXPECT_TRUE(GetLongPressAffordance()->is_animating());
  EXPECT_EQ(window1, GetLongPressAffordanceTarget());

  // Animate to completion.
  GetLongPressAffordance()->End();  // end grow animation.
  // Force timeout to start shrink animation.
  EXPECT_TRUE(timer->IsRunning());
  timer->user_task().Run();
  timer->Stop();
  EXPECT_TRUE(GetLongPressAffordance()->is_animating());
  GetLongPressAffordance()->End();  // end shrink animation.

  // Check if state has reset.
  EXPECT_EQ(NULL, GetLongPressAffordanceTarget());
  EXPECT_EQ(NULL, GetLongPressAffordanceView());
}

TEST_P(SystemGestureEventFilterTest, TwoFingerDrag) {
  gfx::Rect bounds(0, 0, 600, 600);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  views::Widget* toplevel = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, bounds);
  toplevel->Show();

  const int kSteps = 15;
  const int kTouchPoints = 2;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(250, 250),
    gfx::Point(350, 350),
  };

  aura::test::EventGenerator generator(root_window,
                                       toplevel->GetNativeWindow());

  wm::WindowState* toplevel_state =
      wm::GetWindowState(toplevel->GetNativeWindow());
  // Swipe down to minimize.
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 0, 150);
  EXPECT_TRUE(toplevel_state->IsMinimized());

  toplevel->Restore();
  toplevel->GetNativeWindow()->SetBounds(bounds);

  // Swipe up to maximize.
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 0, -150);
  EXPECT_TRUE(toplevel_state->IsMaximized());

  toplevel->Restore();
  toplevel->GetNativeWindow()->SetBounds(bounds);

  // Swipe right to snap.
  gfx::Rect normal_bounds = toplevel->GetWindowBoundsInScreen();
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 150, 0);
  gfx::Rect right_tile_bounds = toplevel->GetWindowBoundsInScreen();
  EXPECT_NE(normal_bounds.ToString(), right_tile_bounds.ToString());

  // Swipe left to snap.
  gfx::Point left_points[kTouchPoints];
  for (int i = 0; i < kTouchPoints; ++i) {
    left_points[i] = points[i];
    left_points[i].Offset(right_tile_bounds.x(), right_tile_bounds.y());
  }
  generator.GestureMultiFingerScroll(kTouchPoints, left_points, 15, kSteps,
      -150, 0);
  gfx::Rect left_tile_bounds = toplevel->GetWindowBoundsInScreen();
  EXPECT_NE(normal_bounds.ToString(), left_tile_bounds.ToString());
  EXPECT_NE(right_tile_bounds.ToString(), left_tile_bounds.ToString());

  // Swipe right again.
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 150, 0);
  gfx::Rect current_bounds = toplevel->GetWindowBoundsInScreen();
  EXPECT_NE(current_bounds.ToString(), left_tile_bounds.ToString());
  EXPECT_EQ(current_bounds.ToString(), right_tile_bounds.ToString());
}

TEST_P(SystemGestureEventFilterTest, TwoFingerDragTwoWindows) {
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  ui::GestureConfiguration::set_max_separation_for_gesture_touches_in_pixels(0);
  views::Widget* first = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, gfx::Rect(10, 0, 50, 100));
  first->Show();
  views::Widget* second = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, gfx::Rect(100, 0, 100, 100));
  second->Show();

  // Start a two-finger drag on |first|, and then try to use another two-finger
  // drag to move |second|. The attempt to move |second| should fail.
  const gfx::Rect& first_bounds = first->GetWindowBoundsInScreen();
  const gfx::Rect& second_bounds = second->GetWindowBoundsInScreen();
  const int kSteps = 15;
  const int kTouchPoints = 4;
  gfx::Point points[kTouchPoints] = {
    first_bounds.origin() + gfx::Vector2d(5, 5),
    first_bounds.origin() + gfx::Vector2d(30, 10),
    second_bounds.origin() + gfx::Vector2d(5, 5),
    second_bounds.origin() + gfx::Vector2d(40, 20)
  };

  aura::test::EventGenerator generator(root_window);
  // Do not drag too fast to avoid fling.
  generator.GestureMultiFingerScroll(kTouchPoints, points,
      50, kSteps, 0, 150);

  EXPECT_NE(first_bounds.ToString(),
            first->GetWindowBoundsInScreen().ToString());
  EXPECT_EQ(second_bounds.ToString(),
            second->GetWindowBoundsInScreen().ToString());
}

TEST_P(SystemGestureEventFilterTest, WindowsWithMaxSizeDontSnap) {
  gfx::Rect bounds(250, 150, 100, 100);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  views::Widget* toplevel = views::Widget::CreateWindowWithContextAndBounds(
      new MaxSizeWidgetDelegate, root_window, bounds);
  toplevel->Show();

  const int kSteps = 15;
  const int kTouchPoints = 2;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(bounds.x() + 10, bounds.y() + 30),
    gfx::Point(bounds.x() + 30, bounds.y() + 20),
  };

  aura::test::EventGenerator generator(root_window,
                                       toplevel->GetNativeWindow());

  // Swipe down to minimize.
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 0, 150);
  wm::WindowState* toplevel_state =
      wm::GetWindowState(toplevel->GetNativeWindow());
  EXPECT_TRUE(toplevel_state->IsMinimized());

  toplevel->Restore();
  toplevel->GetNativeWindow()->SetBounds(bounds);

  // Check that swiping up doesn't maximize.
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 0, -150);
  EXPECT_FALSE(toplevel_state->IsMaximized());

  toplevel->Restore();
  toplevel->GetNativeWindow()->SetBounds(bounds);

  // Check that swiping right doesn't snap.
  gfx::Rect normal_bounds = toplevel->GetWindowBoundsInScreen();
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 150, 0);
  normal_bounds.set_x(normal_bounds.x() + 150);
  EXPECT_EQ(normal_bounds.ToString(),
      toplevel->GetWindowBoundsInScreen().ToString());

  toplevel->GetNativeWindow()->SetBounds(bounds);

  // Check that swiping left doesn't snap.
  normal_bounds = toplevel->GetWindowBoundsInScreen();
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, -150, 0);
  normal_bounds.set_x(normal_bounds.x() - 150);
  EXPECT_EQ(normal_bounds.ToString(),
      toplevel->GetWindowBoundsInScreen().ToString());

  toplevel->GetNativeWindow()->SetBounds(bounds);

  // Swipe right again, make sure the window still doesn't snap.
  normal_bounds = toplevel->GetWindowBoundsInScreen();
  normal_bounds.set_x(normal_bounds.x() + 150);
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 150, 0);
  EXPECT_EQ(normal_bounds.ToString(),
      toplevel->GetWindowBoundsInScreen().ToString());
}

TEST_P(SystemGestureEventFilterTest, TwoFingerDragEdge) {
  gfx::Rect bounds(0, 0, 100, 100);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  views::Widget* toplevel = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, bounds);
  toplevel->Show();

  const int kSteps = 15;
  const int kTouchPoints = 2;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(30, 20),  // Caption
    gfx::Point(0, 40),   // Left edge
  };

  EXPECT_EQ(HTLEFT, toplevel->GetNativeWindow()->delegate()->
        GetNonClientComponent(points[1]));

  aura::test::EventGenerator generator(root_window,
                                       toplevel->GetNativeWindow());

  bounds = toplevel->GetNativeWindow()->bounds();
  // Swipe down. Nothing should happen.
  generator.GestureMultiFingerScroll(kTouchPoints, points, 15, kSteps, 0, 150);
  EXPECT_EQ(bounds.ToString(),
            toplevel->GetNativeWindow()->bounds().ToString());
}

// We do not allow resizing a window via multiple edges simultaneously. Test
// that the behavior is reasonable if a user attempts to resize a window via
// several edges.
TEST_P(SystemGestureEventFilterTest,
       TwoFingerAttemptResizeLeftAndRightEdgesSimultaneously) {
  gfx::Rect initial_bounds(0, 0, 400, 400);
  views::Widget* toplevel =
      views::Widget::CreateWindowWithContextAndBounds(
          new ResizableWidgetDelegate, CurrentContext(), initial_bounds);
  toplevel->Show();

  const int kSteps = 15;
  const int kTouchPoints = 2;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(0, 40),    // Left edge
    gfx::Point(399, 40),  // Right edge
  };
  int delays[kTouchPoints] = {0, 120};

  EXPECT_EQ(HTLEFT, toplevel->GetNonClientComponent(points[0]));
  EXPECT_EQ(HTRIGHT, toplevel->GetNonClientComponent(points[1]));

  GetEventGenerator().GestureMultiFingerScrollWithDelays(
      kTouchPoints, points, delays, 15, kSteps, 0, 40);

  // The window bounds should not have changed because neither of the fingers
  // moved horizontally.
  EXPECT_EQ(initial_bounds.ToString(),
            toplevel->GetNativeWindow()->bounds().ToString());
}

TEST_P(SystemGestureEventFilterTest, TwoFingerDragDelayed) {
  gfx::Rect bounds(0, 0, 100, 100);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  views::Widget* toplevel = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, bounds);
  toplevel->Show();

  const int kSteps = 15;
  const int kTouchPoints = 2;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(30, 20),  // Caption
    gfx::Point(34, 20),  // Caption
  };
  int delays[kTouchPoints] = {0, 120};

  EXPECT_EQ(HTCAPTION, toplevel->GetNativeWindow()->delegate()->
        GetNonClientComponent(points[0]));
  EXPECT_EQ(HTCAPTION, toplevel->GetNativeWindow()->delegate()->
        GetNonClientComponent(points[1]));

  aura::test::EventGenerator generator(root_window,
                                       toplevel->GetNativeWindow());

  bounds = toplevel->GetNativeWindow()->bounds();
  // Swipe right and down starting with one finger.
  // Add another finger after 120ms and continue dragging.
  // The window should move and the drag should be determined by the center
  // point between the fingers.
  generator.GestureMultiFingerScrollWithDelays(
      kTouchPoints, points, delays, 15, kSteps, 150, 150);
  bounds += gfx::Vector2d(150 + (points[1].x() - points[0].x()) / 2, 150);
  EXPECT_EQ(bounds.ToString(),
            toplevel->GetNativeWindow()->bounds().ToString());
}

TEST_P(SystemGestureEventFilterTest, ThreeFingerGestureStopsDrag) {
  gfx::Rect bounds(0, 0, 100, 100);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  views::Widget* toplevel = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, bounds);
  toplevel->Show();

  const int kSteps = 10;
  const int kTouchPoints = 3;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(30, 20),  // Caption
    gfx::Point(34, 20),  // Caption
    gfx::Point(38, 20),  // Caption
  };
  int delays[kTouchPoints] = {0, 0, 120};

  EXPECT_EQ(HTCAPTION, toplevel->GetNativeWindow()->delegate()->
        GetNonClientComponent(points[0]));
  EXPECT_EQ(HTCAPTION, toplevel->GetNativeWindow()->delegate()->
        GetNonClientComponent(points[1]));

  aura::test::EventGenerator generator(root_window,
                                       toplevel->GetNativeWindow());

  bounds = toplevel->GetNativeWindow()->bounds();
  // Swipe right and down starting with two fingers.
  // Add third finger after 120ms and continue dragging.
  // The window should start moving but stop when the 3rd finger touches down.
  const int kEventSeparation = 15;
  generator.GestureMultiFingerScrollWithDelays(
      kTouchPoints, points, delays, kEventSeparation, kSteps, 150, 150);
  int expected_drag = 150 / kSteps * 120 / kEventSeparation;
  bounds += gfx::Vector2d(expected_drag, expected_drag);
  EXPECT_EQ(bounds.ToString(),
            toplevel->GetNativeWindow()->bounds().ToString());
}

TEST_P(SystemGestureEventFilterTest, DragLeftNearEdgeSnaps) {
  gfx::Rect bounds(200, 150, 400, 100);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  views::Widget* toplevel = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, bounds);
  toplevel->Show();

  const int kSteps = 15;
  const int kTouchPoints = 2;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(bounds.x() + bounds.width() / 2, bounds.y() + 5),
    gfx::Point(bounds.x() + bounds.width() / 2, bounds.y() + 5),
  };
  aura::Window* toplevel_window = toplevel->GetNativeWindow();
  aura::test::EventGenerator generator(root_window, toplevel_window);

  // Check that dragging left snaps before reaching the screen edge.
  gfx::Rect work_area =
      Shell::GetScreen()->GetDisplayNearestWindow(root_window).work_area();
  int drag_x = work_area.x() + 20 - points[0].x();
  generator.GestureMultiFingerScroll(
      kTouchPoints, points, 120, kSteps, drag_x, 0);

  EXPECT_EQ(wm::GetDefaultLeftSnappedWindowBoundsInParent(
                toplevel_window).ToString(),
            toplevel_window->bounds().ToString());
}

TEST_P(SystemGestureEventFilterTest, DragRightNearEdgeSnaps) {
  gfx::Rect bounds(200, 150, 400, 100);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  views::Widget* toplevel = views::Widget::CreateWindowWithContextAndBounds(
      new ResizableWidgetDelegate, root_window, bounds);
  toplevel->Show();

  const int kSteps = 15;
  const int kTouchPoints = 2;
  gfx::Point points[kTouchPoints] = {
    gfx::Point(bounds.x() + bounds.width() / 2, bounds.y() + 5),
    gfx::Point(bounds.x() + bounds.width() / 2, bounds.y() + 5),
  };
  aura::Window* toplevel_window = toplevel->GetNativeWindow();
  aura::test::EventGenerator generator(root_window, toplevel_window);

  // Check that dragging right snaps before reaching the screen edge.
  gfx::Rect work_area =
      Shell::GetScreen()->GetDisplayNearestWindow(root_window).work_area();
  int drag_x = work_area.right() - 20 - points[0].x();
  generator.GestureMultiFingerScroll(
      kTouchPoints, points, 120, kSteps, drag_x, 0);
  EXPECT_EQ(wm::GetDefaultRightSnappedWindowBoundsInParent(
                toplevel_window).ToString(),
            toplevel_window->bounds().ToString());
}

// Tests that the window manager does not consume gesture events targetted to
// windows of type WINDOW_TYPE_CONTROL. This is important because the web
// contents are often (but not always) of type WINDOW_TYPE_CONTROL.
TEST_P(SystemGestureEventFilterTest,
       ControlWindowGetsMultiFingerGestureEvents) {
  scoped_ptr<aura::Window> parent(
      CreateTestWindowInShellWithBounds(gfx::Rect(100, 100)));

  aura::test::EventCountDelegate delegate;
  delegate.set_window_component(HTCLIENT);
  scoped_ptr<aura::Window> child(new aura::Window(&delegate));
  child->SetType(ui::wm::WINDOW_TYPE_CONTROL);
  child->Init(aura::WINDOW_LAYER_TEXTURED);
  parent->AddChild(child.get());
  child->SetBounds(gfx::Rect(100, 100));
  child->Show();

  ui::test::TestEventHandler event_handler;
  aura::Env::GetInstance()->PrependPreTargetHandler(&event_handler);

  GetEventGenerator().MoveMouseTo(0, 0);
  for (int i = 1; i <= 3; ++i)
    GetEventGenerator().PressTouchId(i);
  for (int i = 1; i <= 3; ++i)
    GetEventGenerator().ReleaseTouchId(i);
  EXPECT_EQ(event_handler.num_gesture_events(),
            delegate.GetGestureCountAndReset());

  aura::Env::GetInstance()->RemovePreTargetHandler(&event_handler);
}

// Tests run twice - with docked windows disabled or enabled.
INSTANTIATE_TEST_CASE_P(DockedWindowsDisabledOrEnabled,
                        SystemGestureEventFilterTest,
                        testing::Bool());

}  // namespace test
}  // namespace ash
