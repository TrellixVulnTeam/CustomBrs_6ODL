# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# pylint: disable=W0401,W0614
from telemetry.page.actions.all_page_actions import *
from telemetry.page import page as page_module
from telemetry.page import page_set as page_set_module


class ToughCompositorPage(page_module.Page):

  def __init__(self, url, page_set):
    super(ToughCompositorPage, self).__init__(url=url, page_set=page_set)
    self.credentials_path = 'data/credentials.json'
    self.user_agent_type = 'mobile'
    self.archive_data_file = 'data/tough_compositor_cases.json'

  def RunNavigateSteps(self, action_runner):
    action_runner.RunAction(NavigateAction())
    # TODO(epenner): Remove this wait (http://crbug.com/366933)
    action_runner.RunAction(WaitAction({'seconds': 5}))

class ToughCompositorScrollPage(ToughCompositorPage):

  def __init__(self, url, page_set):
    super(ToughCompositorScrollPage, self).__init__(url=url, page_set=page_set)

  def RunSmoothness(self, action_runner):
    # Make the scroll longer to reduce noise.
    scroll_down = ScrollAction()
    scroll_down.direction = "down"
    scroll_down.speed = 300
    action_runner.RunAction(scroll_down)

class ToughCompositorWaitPage(ToughCompositorPage):

  def __init__(self, url, page_set):
    super(ToughCompositorWaitPage, self).__init__(url=url, page_set=page_set)

  def RunSmoothness(self, action_runner):
    # We scroll back and forth a few times to reduce noise in the tests.
    action_runner.RunAction(WaitAction({'seconds': 10}))


class ToughCompositorCasesPageSet(page_set_module.PageSet):

  """ Touch compositor sites """

  def __init__(self):
    super(ToughCompositorCasesPageSet, self).__init__(
      credentials_path='data/credentials.json',
      user_agent_type='mobile',
      archive_data_file='data/tough_compositor_cases.json')

    scroll_urls_list = [
      # Why: Baseline CC scrolling page. A long page with only text. """
      'http://jsbin.com/pixavefe/1/quiet?CC_SCROLL_TEXT_ONLY',
      # Why: Baseline JS scrolling page. A long page with only text. """
      'http://jsbin.com/wixadinu/1/quiet?JS_SCROLL_TEXT_ONLY',
      # Why: Scroll by a large number of CC layers """
      'http://jsbin.com/yakagevo/1/quiet?CC_SCROLL_200_LAYER_GRID',
      # Why: Scroll by a large number of JS layers """
      'http://jsbin.com/jevibahi/1/quiet?JS_SCROLL_200_LAYER_GRID',
    ]

    wait_urls_list = [
      # Why: CC Poster circle animates many layers """
      'http://jsbin.com/falefice/1/quiet?CC_POSTER_CIRCLE',
      # Why: JS poster circle animates/commits many layers """
      'http://jsbin.com/giqafofe/1/quiet?JS_POSTER_CIRCLE',
      # Why: JS invalidation does lots of uploads """
      'http://jsbin.com/beqojupo/1/quiet?JS_FULL_SCREEN_INVALIDATION',
    ]

    for url in scroll_urls_list:
      self.AddPage(ToughCompositorScrollPage(url, self))

    for url in wait_urls_list:
      self.AddPage(ToughCompositorWaitPage(url, self))
