// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_VIEWS_CONTENTS_VIEW_H_
#define UI_APP_LIST_VIEWS_CONTENTS_VIEW_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}

namespace views {
class BoundsAnimator;
class ViewModel;
}

namespace app_list {

class AppsGridView;
class ApplicationDragAndDropHost;
class AppListFolderItem;
class AppListMainView;
class AppListModel;
class AppListViewDelegate;
class AppsContainerView;
class PaginationModel;
class StartPageView;

// A view to manage sub views under the search box (apps grid view + page
// switcher and search results). The two sets of sub views are mutually
// exclusive. ContentsView manages a show state to choose one set to show
// and animates the transition between show states.
class ContentsView : public views::View {
 public:
  enum ShowState {
    SHOW_APPS,
    SHOW_SEARCH_RESULTS,
    SHOW_START_PAGE,
  };

  ContentsView(AppListMainView* app_list_main_view,
               PaginationModel* pagination_model,
               AppListModel* model,
               AppListViewDelegate* view_delegate);
  virtual ~ContentsView();

  // The app list gets closed and drag and drop operations need to be cancelled.
  void CancelDrag();

  // If |drag_and_drop| is not NULL it will be called upon drag and drop
  // operations outside the application list.
  void SetDragAndDropHostOfCurrentAppList(
      ApplicationDragAndDropHost* drag_and_drop_host);

  void ShowSearchResults(bool show);
  void ShowFolderContent(AppListFolderItem* folder);

  // Sets show state and animates the subviews to match the show state.
  void SetShowState(ShowState show_state);

  void Prerender();

  AppsContainerView* apps_container_view() { return apps_container_view_; }

  ShowState show_state() const { return show_state_; }

  // Overridden from views::View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual bool OnKeyPressed(const ui::KeyEvent& event) OVERRIDE;
  virtual bool OnMouseWheel(const ui::MouseWheelEvent& event) OVERRIDE;

 private:
  // Invoked when show state is changed.
  void ShowStateChanged();

  void CalculateIdealBounds();
  void AnimateToIdealBounds();

  // Overridden from ui::EventHandler:
  virtual void OnGestureEvent(ui::GestureEvent* event) OVERRIDE;
  virtual void OnScrollEvent(ui::ScrollEvent* event) OVERRIDE;

  ShowState show_state_;
  PaginationModel* pagination_model_;  // Owned by AppListController.

  AppsContainerView* apps_container_view_;  // Owned by the views hierarchy.
  AppListMainView* app_list_main_view_;     // Parent view, owns this.

  scoped_ptr<views::ViewModel> view_model_;
  scoped_ptr<views::BoundsAnimator> bounds_animator_;

  DISALLOW_COPY_AND_ASSIGN(ContentsView);
};

}  // namespace app_list

#endif  // UI_APP_LIST_VIEWS_CONTENTS_VIEW_H_
