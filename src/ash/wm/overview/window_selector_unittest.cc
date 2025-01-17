// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/accessibility_delegate.h"
#include "ash/drag_drop/drag_drop_controller.h"
#include "ash/root_window_controller.h"
#include "ash/screen_util.h"
#include "ash/shelf/shelf.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/shelf_test_api.h"
#include "ash/test/shelf_view_test_api.h"
#include "ash/test/shell_test_api.h"
#include "ash/test/test_shelf_delegate.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/overview/window_selector.h"
#include "ash/wm/overview/window_selector_controller.h"
#include "ash/wm/overview/window_selector_item.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "ash/wm/wm_event.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_vector.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/gfx/rect_conversions.h"
#include "ui/gfx/transform.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/activation_delegate.h"

namespace ash {
namespace {

class NonActivatableActivationDelegate
    : public aura::client::ActivationDelegate {
 public:
  virtual bool ShouldActivate() const OVERRIDE {
    return false;
  }
};

void CancelDrag(DragDropController* controller, bool* canceled) {
  if (controller->IsDragDropInProgress()) {
    *canceled = true;
    controller->DragCancel();
  }
}

}  // namespace

class WindowSelectorTest : public test::AshTestBase {
 public:
  WindowSelectorTest() {}
  virtual ~WindowSelectorTest() {}

  virtual void SetUp() OVERRIDE {
    test::AshTestBase::SetUp();
    ASSERT_TRUE(test::TestShelfDelegate::instance());

    shelf_view_test_.reset(new test::ShelfViewTestAPI(
        test::ShelfTestAPI(Shelf::ForPrimaryDisplay()).shelf_view()));
    shelf_view_test_->SetAnimationDuration(1);
  }

  aura::Window* CreateWindow(const gfx::Rect& bounds) {
    return CreateTestWindowInShellWithDelegate(&delegate_, -1, bounds);
  }

  aura::Window* CreateNonActivatableWindow(const gfx::Rect& bounds) {
    aura::Window* window = CreateWindow(bounds);
    aura::client::SetActivationDelegate(window,
                                        &non_activatable_activation_delegate_);
    EXPECT_FALSE(ash::wm::CanActivateWindow(window));
    return window;
  }

  aura::Window* CreatePanelWindow(const gfx::Rect& bounds) {
    aura::Window* window = CreateTestWindowInShellWithDelegateAndType(
        NULL, ui::wm::WINDOW_TYPE_PANEL, 0, bounds);
    test::TestShelfDelegate::instance()->AddShelfItem(window);
    shelf_view_test()->RunMessageLoopUntilAnimationsDone();
    return window;
  }

  bool WindowsOverlapping(aura::Window* window1, aura::Window* window2) {
    gfx::RectF window1_bounds = GetTransformedTargetBounds(window1);
    gfx::RectF window2_bounds = GetTransformedTargetBounds(window2);
    return window1_bounds.Intersects(window2_bounds);
  }

  void ToggleOverview() {
    ash::Shell::GetInstance()->window_selector_controller()->ToggleOverview();
  }

  gfx::Transform GetTransformRelativeTo(gfx::PointF origin,
                                        const gfx::Transform& transform) {
    gfx::Transform t;
    t.Translate(origin.x(), origin.y());
    t.PreconcatTransform(transform);
    t.Translate(-origin.x(), -origin.y());
    return t;
  }

  gfx::RectF GetTransformedBounds(aura::Window* window) {
    gfx::RectF bounds(ScreenUtil::ConvertRectToScreen(
        window->parent(), window->layer()->bounds()));
    gfx::Transform transform(GetTransformRelativeTo(bounds.origin(),
        window->layer()->transform()));
    transform.TransformRect(&bounds);
    return bounds;
  }

  gfx::RectF GetTransformedTargetBounds(aura::Window* window) {
    gfx::RectF bounds(ScreenUtil::ConvertRectToScreen(
        window->parent(), window->layer()->GetTargetBounds()));
    gfx::Transform transform(GetTransformRelativeTo(bounds.origin(),
        window->layer()->GetTargetTransform()));
    transform.TransformRect(&bounds);
    return bounds;
  }

  gfx::RectF GetTransformedBoundsInRootWindow(aura::Window* window) {
    gfx::RectF bounds = gfx::Rect(window->bounds().size());
    aura::Window* root = window->GetRootWindow();
    CHECK(window->layer());
    CHECK(root->layer());
    gfx::Transform transform;
    if (!window->layer()->GetTargetTransformRelativeTo(root->layer(),
                                                       &transform)) {
      return gfx::RectF();
    }
    transform.TransformRect(&bounds);
    return bounds;
  }

  void ClickWindow(aura::Window* window) {
    aura::test::EventGenerator event_generator(window->GetRootWindow(), window);
    gfx::RectF target = GetTransformedBounds(window);
    event_generator.ClickLeftButton();
  }

  bool IsSelecting() {
    return ash::Shell::GetInstance()->window_selector_controller()->
        IsSelecting();
  }

  aura::Window* GetFocusedWindow() {
    return aura::client::GetFocusClient(
        Shell::GetPrimaryRootWindow())->GetFocusedWindow();
    }

  ScopedVector<WindowSelectorItem>* GetWindowItems() {
    return &(ash::Shell::GetInstance()->window_selector_controller()->
        window_selector_->windows_);
    }

  views::Widget* GetLabelWidget(ash::WindowSelectorItem* window) {
    return window->window_label_.get();
  }

  test::ShelfViewTestAPI* shelf_view_test() {
    return shelf_view_test_.get();
  }

 private:
  aura::test::TestWindowDelegate delegate_;
  NonActivatableActivationDelegate non_activatable_activation_delegate_;
  scoped_ptr<test::ShelfViewTestAPI> shelf_view_test_;

  DISALLOW_COPY_AND_ASSIGN(WindowSelectorTest);
};

// Tests that an a11y alert is sent on entering overview mode.
TEST_F(WindowSelectorTest, A11yAlertOnOverviewMode) {
  gfx::Rect bounds(0, 0, 400, 400);
  AccessibilityDelegate* delegate =
      ash::Shell::GetInstance()->accessibility_delegate();
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  EXPECT_NE(delegate->GetLastAccessibilityAlert(),
            A11Y_ALERT_WINDOW_OVERVIEW_MODE_ENTERED);
  ToggleOverview();
  EXPECT_EQ(delegate->GetLastAccessibilityAlert(),
            A11Y_ALERT_WINDOW_OVERVIEW_MODE_ENTERED);
}

// Tests entering overview mode with two windows and selecting one.
TEST_F(WindowSelectorTest, Basic) {
  gfx::Rect bounds(0, 0, 400, 400);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds));
  scoped_ptr<aura::Window> panel1(CreatePanelWindow(bounds));
  scoped_ptr<aura::Window> panel2(CreatePanelWindow(bounds));
  EXPECT_TRUE(WindowsOverlapping(window1.get(), window2.get()));
  EXPECT_TRUE(WindowsOverlapping(panel1.get(), panel2.get()));
  wm::ActivateWindow(window2.get());
  EXPECT_FALSE(wm::IsActiveWindow(window1.get()));
  EXPECT_TRUE(wm::IsActiveWindow(window2.get()));
  EXPECT_EQ(window2.get(), GetFocusedWindow());
  // Hide the cursor before entering overview to test that it will be shown.
  aura::client::GetCursorClient(root_window)->HideCursor();

  // In overview mode the windows should no longer overlap and focus should
  // be removed from the window.
  ToggleOverview();
  EXPECT_EQ(NULL, GetFocusedWindow());
  EXPECT_FALSE(WindowsOverlapping(window1.get(), window2.get()));
  EXPECT_FALSE(WindowsOverlapping(window1.get(), panel1.get()));
  // Panels 1 and 2 should still be overlapping being in a single selector
  // item.
  EXPECT_TRUE(WindowsOverlapping(panel1.get(), panel2.get()));

  // The cursor should be visible and locked as a pointer
  EXPECT_EQ(ui::kCursorPointer,
            root_window->GetHost()->last_cursor().native_type());
  EXPECT_TRUE(aura::client::GetCursorClient(root_window)->IsCursorLocked());
  EXPECT_TRUE(aura::client::GetCursorClient(root_window)->IsCursorVisible());

  // Clicking window 1 should activate it.
  ClickWindow(window1.get());
  EXPECT_TRUE(wm::IsActiveWindow(window1.get()));
  EXPECT_FALSE(wm::IsActiveWindow(window2.get()));
  EXPECT_EQ(window1.get(), GetFocusedWindow());

  // Cursor should have been unlocked.
  EXPECT_FALSE(aura::client::GetCursorClient(root_window)->IsCursorLocked());
}

// Tests entering overview mode with two windows and selecting one.
TEST_F(WindowSelectorTest, FullscreenWindow) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds));
  scoped_ptr<aura::Window> panel1(CreatePanelWindow(bounds));
  wm::ActivateWindow(window1.get());

  const wm::WMEvent toggle_fullscreen_event(wm::WM_EVENT_TOGGLE_FULLSCREEN);
  wm::GetWindowState(window1.get())->OnWMEvent(&toggle_fullscreen_event);
  // The panel is hidden in fullscreen mode.
  EXPECT_FALSE(panel1->IsVisible());
  EXPECT_TRUE(wm::GetWindowState(window1.get())->IsFullscreen());

  // Enter overview and select the fullscreen window.
  ToggleOverview();

  // The panel becomes temporarily visible for the overview.
  EXPECT_TRUE(panel1->IsVisible());
  ClickWindow(window1.get());

  // The window is still fullscreen as it was selected. The panel should again
  // be hidden.
  EXPECT_TRUE(wm::GetWindowState(window1.get())->IsFullscreen());
  EXPECT_FALSE(panel1->IsVisible());

  // Entering overview and selecting another window, the previous window remains
  // fullscreen.
  // TODO(flackr): Currently the panel remains hidden, but should become visible
  // again.
  ToggleOverview();
  ClickWindow(window2.get());
  EXPECT_TRUE(wm::GetWindowState(window1.get())->IsFullscreen());
}

// Tests that the shelf dimming state is removed while in overview and restored
// on exiting overview.
TEST_F(WindowSelectorTest, OverviewUndimsShelf) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  wm::WindowState* window_state = wm::GetWindowState(window1.get());
  window_state->Maximize();
  ash::ShelfWidget* shelf = Shell::GetPrimaryRootWindowController()->shelf();
  EXPECT_TRUE(shelf->GetDimsShelf());
  ToggleOverview();
  EXPECT_FALSE(shelf->GetDimsShelf());
  ToggleOverview();
  EXPECT_TRUE(shelf->GetDimsShelf());
}

// Tests that beginning window selection hides the app list.
TEST_F(WindowSelectorTest, SelectingHidesAppList) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds));
  Shell::GetInstance()->ToggleAppList(NULL);
  EXPECT_TRUE(Shell::GetInstance()->GetAppListTargetVisibility());
  ToggleOverview();
  EXPECT_FALSE(Shell::GetInstance()->GetAppListTargetVisibility());
  ToggleOverview();
}

// Tests that a minimized window's visibility and layer visibility is correctly
// changed when entering overview and restored when leaving overview mode.
TEST_F(WindowSelectorTest, MinimizedWindowVisibility) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  wm::WindowState* window_state = wm::GetWindowState(window1.get());
  window_state->Minimize();
  EXPECT_FALSE(window1->IsVisible());
  EXPECT_FALSE(window1->layer()->GetTargetVisibility());
  {
    ui::ScopedAnimationDurationScaleMode normal_duration_mode(
        ui::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
    ToggleOverview();
    EXPECT_TRUE(window1->IsVisible());
    EXPECT_TRUE(window1->layer()->GetTargetVisibility());
  }
  {
    ui::ScopedAnimationDurationScaleMode normal_duration_mode(
        ui::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
    ToggleOverview();
    EXPECT_FALSE(window1->IsVisible());
    EXPECT_FALSE(window1->layer()->GetTargetVisibility());
  }
}

// Tests that a bounds change during overview is corrected for.
TEST_F(WindowSelectorTest, BoundsChangeDuringOverview) {
  scoped_ptr<aura::Window> window(CreateWindow(gfx::Rect(0, 0, 400, 400)));
  ToggleOverview();
  gfx::Rect overview_bounds =
      ToEnclosingRect(GetTransformedTargetBounds(window.get()));
  window->SetBounds(gfx::Rect(200, 0, 200, 200));
  gfx::Rect new_overview_bounds =
      ToEnclosingRect(GetTransformedTargetBounds(window.get()));
  EXPECT_EQ(overview_bounds.x(), new_overview_bounds.x());
  EXPECT_EQ(overview_bounds.y(), new_overview_bounds.y());
  EXPECT_EQ(overview_bounds.width(), new_overview_bounds.width());
  EXPECT_EQ(overview_bounds.height(), new_overview_bounds.height());
  ToggleOverview();
}

// Tests that a newly created window aborts overview.
TEST_F(WindowSelectorTest, NewWindowCancelsOveriew) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds));
  ToggleOverview();
  EXPECT_TRUE(IsSelecting());

  // A window being created should exit overview mode.
  scoped_ptr<aura::Window> window3(CreateWindow(bounds));
  EXPECT_FALSE(IsSelecting());
}

// Tests that a window activation exits overview mode.
TEST_F(WindowSelectorTest, ActivationCancelsOveriew) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds));
  window2->Focus();
  ToggleOverview();
  EXPECT_TRUE(IsSelecting());

  // A window being activated should exit overview mode.
  window1->Focus();
  EXPECT_FALSE(IsSelecting());

  // window1 should be focused after exiting even though window2 was focused on
  // entering overview because we exited due to an activation.
  EXPECT_EQ(window1.get(), GetFocusedWindow());
}

// Tests that exiting overview mode without selecting a window restores focus
// to the previously focused window.
TEST_F(WindowSelectorTest, CancelRestoresFocus) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window(CreateWindow(bounds));
  wm::ActivateWindow(window.get());
  EXPECT_EQ(window.get(), GetFocusedWindow());

  // In overview mode, focus should be removed.
  ToggleOverview();
  EXPECT_EQ(NULL, GetFocusedWindow());

  // If canceling overview mode, focus should be restored.
  ToggleOverview();
  EXPECT_EQ(window.get(), GetFocusedWindow());
}

// Tests that overview mode is exited if the last remaining window is destroyed.
TEST_F(WindowSelectorTest, LastWindowDestroyed) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds));
  ToggleOverview();

  window1.reset();
  window2.reset();
  EXPECT_FALSE(IsSelecting());
}

// Tests that entering overview mode restores a window to its original
// target location.
TEST_F(WindowSelectorTest, QuickReentryRestoresInitialTransform) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window(CreateWindow(bounds));
  gfx::Rect initial_bounds = ToEnclosingRect(
      GetTransformedBounds(window.get()));
  ToggleOverview();
  // Quickly exit and reenter overview mode. The window should still be
  // animating when we reenter. We cannot short circuit animations for this but
  // we also don't have to wait for them to complete.
  {
    ui::ScopedAnimationDurationScaleMode normal_duration_mode(
        ui::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
    ToggleOverview();
    ToggleOverview();
  }
  EXPECT_NE(initial_bounds, ToEnclosingRect(
      GetTransformedTargetBounds(window.get())));
  ToggleOverview();
  EXPECT_FALSE(IsSelecting());
  EXPECT_EQ(initial_bounds, ToEnclosingRect(
      GetTransformedTargetBounds(window.get())));
}

// Tests that non-activatable windows are hidden when entering overview mode.
TEST_F(WindowSelectorTest, NonActivatableWindowsHidden) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds));
  scoped_ptr<aura::Window> non_activatable_window(
      CreateNonActivatableWindow(Shell::GetPrimaryRootWindow()->bounds()));
  EXPECT_TRUE(non_activatable_window->IsVisible());
  ToggleOverview();
  EXPECT_FALSE(non_activatable_window->IsVisible());
  ToggleOverview();
  EXPECT_TRUE(non_activatable_window->IsVisible());

  // Test that a window behind the fullscreen non-activatable window can be
  // clicked.
  non_activatable_window->parent()->StackChildAtTop(
      non_activatable_window.get());
  ToggleOverview();
  ClickWindow(window1.get());
  EXPECT_FALSE(IsSelecting());
  EXPECT_TRUE(wm::IsActiveWindow(window1.get()));
}

// Tests that windows with modal child windows are transformed with the modal
// child even though not activatable themselves.
TEST_F(WindowSelectorTest, ModalChild) {
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds));
  scoped_ptr<aura::Window> child1(CreateWindow(bounds));
  child1->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_WINDOW);
  ::wm::AddTransientChild(window1.get(), child1.get());
  EXPECT_EQ(window1->parent(), child1->parent());
  ToggleOverview();
  EXPECT_TRUE(window1->IsVisible());
  EXPECT_TRUE(child1->IsVisible());
  EXPECT_EQ(ToEnclosingRect(GetTransformedTargetBounds(child1.get())),
      ToEnclosingRect(GetTransformedTargetBounds(window1.get())));
  ToggleOverview();
}

// Tests that clicking a modal window's parent activates the modal window in
// overview.
TEST_F(WindowSelectorTest, ClickModalWindowParent) {
  scoped_ptr<aura::Window> window1(CreateWindow(gfx::Rect(0, 0, 180, 180)));
  scoped_ptr<aura::Window> child1(CreateWindow(gfx::Rect(200, 0, 180, 180)));
  child1->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_WINDOW);
  ::wm::AddTransientChild(window1.get(), child1.get());
  EXPECT_FALSE(WindowsOverlapping(window1.get(), child1.get()));
  EXPECT_EQ(window1->parent(), child1->parent());
  ToggleOverview();
  // Given that their relative positions are preserved, the windows should still
  // not overlap.
  EXPECT_FALSE(WindowsOverlapping(window1.get(), child1.get()));
  ClickWindow(window1.get());
  EXPECT_FALSE(IsSelecting());

  // Clicking on window1 should activate child1.
  EXPECT_TRUE(wm::IsActiveWindow(child1.get()));
}

// Tests that windows remain on the display they are currently on in overview
// mode.
TEST_F(WindowSelectorTest, MultipleDisplays) {
  if (!SupportsMultipleDisplays())
    return;

  UpdateDisplay("600x400,600x400");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  gfx::Rect bounds1(0, 0, 400, 400);
  gfx::Rect bounds2(650, 0, 400, 400);

  scoped_ptr<aura::Window> window1(CreateWindow(bounds1));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds1));
  scoped_ptr<aura::Window> window3(CreateWindow(bounds2));
  scoped_ptr<aura::Window> window4(CreateWindow(bounds2));
  scoped_ptr<aura::Window> panel1(CreatePanelWindow(bounds1));
  scoped_ptr<aura::Window> panel2(CreatePanelWindow(bounds1));
  scoped_ptr<aura::Window> panel3(CreatePanelWindow(bounds2));
  scoped_ptr<aura::Window> panel4(CreatePanelWindow(bounds2));
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[0], window2->GetRootWindow());
  EXPECT_EQ(root_windows[1], window3->GetRootWindow());
  EXPECT_EQ(root_windows[1], window4->GetRootWindow());

  EXPECT_EQ(root_windows[0], panel1->GetRootWindow());
  EXPECT_EQ(root_windows[0], panel2->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel3->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel4->GetRootWindow());

  // In overview mode, each window remains in the same root window.
  ToggleOverview();
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[0], window2->GetRootWindow());
  EXPECT_EQ(root_windows[1], window3->GetRootWindow());
  EXPECT_EQ(root_windows[1], window4->GetRootWindow());
  EXPECT_EQ(root_windows[0], panel1->GetRootWindow());
  EXPECT_EQ(root_windows[0], panel2->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel3->GetRootWindow());
  EXPECT_EQ(root_windows[1], panel4->GetRootWindow());

  EXPECT_TRUE(root_windows[0]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(window1.get()))));
  EXPECT_TRUE(root_windows[0]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(window2.get()))));
  EXPECT_TRUE(root_windows[1]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(window3.get()))));
  EXPECT_TRUE(root_windows[1]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(window4.get()))));

  EXPECT_TRUE(root_windows[0]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(panel1.get()))));
  EXPECT_TRUE(root_windows[0]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(panel2.get()))));
  EXPECT_TRUE(root_windows[1]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(panel3.get()))));
  EXPECT_TRUE(root_windows[1]->GetBoundsInScreen().Contains(
      ToEnclosingRect(GetTransformedTargetBounds(panel4.get()))));
  EXPECT_TRUE(WindowsOverlapping(panel1.get(), panel2.get()));
  EXPECT_TRUE(WindowsOverlapping(panel3.get(), panel4.get()));
  EXPECT_FALSE(WindowsOverlapping(panel1.get(), panel3.get()));
}

// Tests shutting down during overview.
TEST_F(WindowSelectorTest, Shutdown) {
  gfx::Rect bounds(0, 0, 400, 400);
  // These windows will be deleted when the test exits and the Shell instance
  // is shut down.
  aura::Window* window1(CreateWindow(bounds));
  aura::Window* window2(CreateWindow(bounds));
  aura::Window* window3(CreatePanelWindow(bounds));
  aura::Window* window4(CreatePanelWindow(bounds));

  wm::ActivateWindow(window4);
  wm::ActivateWindow(window3);
  wm::ActivateWindow(window2);
  wm::ActivateWindow(window1);

  ToggleOverview();
}

// Tests removing a display during overview.
TEST_F(WindowSelectorTest, RemoveDisplay) {
  if (!SupportsMultipleDisplays())
    return;

  UpdateDisplay("400x400,400x400");
  gfx::Rect bounds1(0, 0, 100, 100);
  gfx::Rect bounds2(450, 0, 100, 100);
  scoped_ptr<aura::Window> window1(CreateWindow(bounds1));
  scoped_ptr<aura::Window> window2(CreateWindow(bounds2));
  scoped_ptr<aura::Window> window3(CreatePanelWindow(bounds1));
  scoped_ptr<aura::Window> window4(CreatePanelWindow(bounds2));

  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[1], window2->GetRootWindow());
  EXPECT_EQ(root_windows[0], window3->GetRootWindow());
  EXPECT_EQ(root_windows[1], window4->GetRootWindow());

  wm::ActivateWindow(window4.get());
  wm::ActivateWindow(window3.get());
  wm::ActivateWindow(window2.get());
  wm::ActivateWindow(window1.get());

  ToggleOverview();
  EXPECT_TRUE(IsSelecting());
  UpdateDisplay("400x400");
  EXPECT_FALSE(IsSelecting());
}

// Tests starting overview during a drag and drop tracking operation.
// TODO(flackr): Fix memory corruption crash when running locally (not failing
// on bots). See http://crbug.com/342528.
TEST_F(WindowSelectorTest, DISABLED_DragDropInProgress) {
  bool drag_canceled_by_test = false;
  gfx::Rect bounds(0, 0, 400, 400);
  scoped_ptr<aura::Window> window(CreateWindow(bounds));
  test::ShellTestApi shell_test_api(Shell::GetInstance());
  ash::DragDropController* drag_drop_controller =
      shell_test_api.drag_drop_controller();
  ui::OSExchangeData data;
  base::MessageLoopForUI::current()->PostTask(FROM_HERE,
      base::Bind(&WindowSelectorTest::ToggleOverview,
                 base::Unretained(this)));
  base::MessageLoopForUI::current()->PostTask(FROM_HERE,
      base::Bind(&CancelDrag, drag_drop_controller, &drag_canceled_by_test));
  data.SetString(base::UTF8ToUTF16("I am being dragged"));
  drag_drop_controller->StartDragAndDrop(data, window->GetRootWindow(),
      window.get(), gfx::Point(5, 5), ui::DragDropTypes::DRAG_MOVE,
      ui::DragDropTypes::DRAG_EVENT_SOURCE_MOUSE);
  RunAllPendingInMessageLoop();
  EXPECT_FALSE(drag_canceled_by_test);
  ASSERT_TRUE(IsSelecting());
  RunAllPendingInMessageLoop();
}

TEST_F(WindowSelectorTest, HitTestingInOverview) {
  gfx::Rect window_bounds(20, 10, 200, 300);
  aura::Window* root_window = Shell::GetPrimaryRootWindow();
  scoped_ptr<aura::Window> window1(CreateWindow(window_bounds));
  scoped_ptr<aura::Window> window2(CreateWindow(window_bounds));

  ToggleOverview();
  gfx::RectF bounds1 = GetTransformedBoundsInRootWindow(window1.get());
  gfx::RectF bounds2 = GetTransformedBoundsInRootWindow(window2.get());
  EXPECT_NE(bounds1.ToString(), bounds2.ToString());

  ui::EventTarget* root_target = root_window;
  ui::EventTargeter* targeter = root_target->GetEventTargeter();
  aura::Window* windows[] = { window1.get(), window2.get() };
  for (size_t w = 0; w < arraysize(windows); ++w) {
    gfx::RectF bounds = GetTransformedBoundsInRootWindow(windows[w]);
    // The close button covers the top-right corner of the window so we skip
    // this in hit testing.
    gfx::Point points[] = {
      gfx::Point(bounds.x(), bounds.y()),
      gfx::Point(bounds.x(), bounds.bottom() - 1),
      gfx::Point(bounds.right() - 1, bounds.bottom() - 1),
    };

    for (size_t p = 0; p < arraysize(points); ++p) {
      ui::MouseEvent event(ui::ET_MOUSE_MOVED, points[p], points[p],
                           ui::EF_NONE, ui::EF_NONE);
      EXPECT_EQ(windows[w],
                targeter->FindTargetForEvent(root_target, &event));
    }
  }
}

// Test that a label is created under the window on entering overview mode.
TEST_F(WindowSelectorTest, CreateLabelUnderWindow) {
  scoped_ptr<aura::Window> window(CreateWindow(gfx::Rect(0, 0, 100, 100)));
  base::string16 window_title = base::UTF8ToUTF16("My window");
  window->set_title(window_title);
  ToggleOverview();
  WindowSelectorItem* window_item = GetWindowItems()->back();
  views::Widget* widget = GetLabelWidget(window_item);
  // Has the label widget been created?
  ASSERT_TRUE(widget);
  views::Label* label = static_cast<views::Label*>(widget->GetContentsView());
  // Verify the label matches the window title.
  EXPECT_EQ(label->text(), window_title);
  // Labels are located based on target_bounds, not the actual window item
  // bounds.
  gfx::Rect target_bounds(window_item->target_bounds());
  gfx::Rect expected_label_bounds(target_bounds.x(),
                                  target_bounds.bottom(),
                                  target_bounds.width(),
                                  label->GetPreferredSize().height());
  gfx::Rect real_label_bounds = widget->GetNativeWindow()->bounds();
  EXPECT_EQ(widget->GetNativeWindow()->bounds(), real_label_bounds);
}

// Tests that a label is created for the active panel in a group of panels in
// overview mode.
TEST_F(WindowSelectorTest, CreateLabelUnderPanel) {
  scoped_ptr<aura::Window> panel1(CreatePanelWindow(gfx::Rect(0, 0, 100, 100)));
  scoped_ptr<aura::Window> panel2(CreatePanelWindow(gfx::Rect(0, 0, 100, 100)));
  base::string16 panel1_title = base::UTF8ToUTF16("My panel");
  base::string16 panel2_title = base::UTF8ToUTF16("Another panel");
  panel1->set_title(panel1_title);
  panel2->set_title(panel2_title);
  wm::ActivateWindow(panel1.get());
  ToggleOverview();
  WindowSelectorItem* window_item = GetWindowItems()->back();
  views::Widget* widget = GetLabelWidget(window_item);
  // Has the label widget been created?
  ASSERT_TRUE(widget);
  views::Label* label = static_cast<views::Label*>(widget->GetContentsView());
  // Verify the label matches the active window title.
  EXPECT_EQ(label->text(), panel1_title);
}

// Tests that overview updates the window postions if the display orientation
// changes.
TEST_F(WindowSelectorTest, DisplayOrientationChanged) {
  if (!SupportsHostWindowResize())
    return;

  aura::Window* root_window = Shell::GetInstance()->GetPrimaryRootWindow();
  UpdateDisplay("600x200");
  EXPECT_EQ("0,0 600x200", root_window->bounds().ToString());
  gfx::Rect window_bounds(0, 0, 150, 150);
  ScopedVector<aura::Window> windows;
  for (int i = 0; i < 3; i++) {
    windows.push_back(CreateWindow(window_bounds));
  }

  ToggleOverview();
  for (ScopedVector<aura::Window>::iterator iter = windows.begin();
       iter != windows.end(); ++iter) {
    EXPECT_TRUE(root_window->bounds().Contains(
        ToEnclosingRect(GetTransformedTargetBounds(*iter))));
  }

  // Rotate the display, windows should be repositioned to be within the screen
  // bounds.
  UpdateDisplay("600x200/r");
  EXPECT_EQ("0,0 200x600", root_window->bounds().ToString());
  for (ScopedVector<aura::Window>::iterator iter = windows.begin();
       iter != windows.end(); ++iter) {
    EXPECT_TRUE(root_window->bounds().Contains(
        ToEnclosingRect(GetTransformedTargetBounds(*iter))));
  }
}

}  // namespace ash
