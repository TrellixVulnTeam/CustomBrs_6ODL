// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_TEST_VIEWS_TEST_HELPER_AURA_H_
#define UI_VIEWS_TEST_VIEWS_TEST_HELPER_AURA_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ui/views/test/views_test_helper.h"

namespace aura {
namespace test {
class AuraTestHelper;
}
}

namespace base {
class MessageLoopForUI;
}

namespace wm {
class WMState;
}

namespace views {

class ViewsTestHelperAura : public ViewsTestHelper {
 public:
  explicit ViewsTestHelperAura(base::MessageLoopForUI* message_loop);
  virtual ~ViewsTestHelperAura();

  // Overridden from ViewsTestHelper:
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;
  virtual gfx::NativeView GetContext() OVERRIDE;

 private:
  scoped_ptr<aura::test::AuraTestHelper> aura_test_helper_;
  scoped_ptr<wm::WMState> wm_state_;

  DISALLOW_COPY_AND_ASSIGN(ViewsTestHelperAura);
};

}  // namespace views

#endif  // UI_VIEWS_TEST_VIEWS_TEST_HELPER_AURA_H_
