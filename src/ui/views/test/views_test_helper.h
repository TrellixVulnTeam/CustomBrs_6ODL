// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_TEST_VIEWS_TEST_HELPER_H_
#define UI_VIEWS_TEST_VIEWS_TEST_HELPER_H_

#include "ui/gfx/native_widget_types.h"

namespace base {
class MessageLoopForUI;
}

namespace views {

// A helper class owned by tests that performs platform specific initialization
// required for running tests.
class ViewsTestHelper {
 public:
  explicit ViewsTestHelper();
  virtual ~ViewsTestHelper();

  // Create a platform specific instance.
  static ViewsTestHelper* Create(base::MessageLoopForUI* message_loop);

  // Creates objects that are needed for tests.
  virtual void SetUp();

  // Clean up objects that were created for tests.
  virtual void TearDown();

  // Returns a context view. In aura builds, this will be the
  // RootWindow. Everywhere else, NULL.
  virtual gfx::NativeView GetContext();

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewsTestHelper);
};

}  // namespace views

#endif  // UI_VIEWS_TEST_VIEWS_TEST_HELPER_H_
