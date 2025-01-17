// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do not test orientation or hover attributes (similar to exclusions on native
// accessibility), since they can be inconsistent depending on the environment.
var RemoveUntestedStates = function(state) {
  delete state['horizontal'];
  delete state['hovered'];
  delete state['vertical'];
};

var allTests = [
  function testSimplePage() {
    var title = tree.root.attributes['docTitle'];
    assertEq('Automation Tests', title);
    RemoveUntestedStates(tree.root.state);
    assertEq(
      {enabled: true, focusable: true, readOnly: true},
      tree.root.state);
    var children = tree.root.children();
    assertEq(1, children.length);

    var body = children[0];
    assertEq('body', body.attributes['htmlTag']);

    RemoveUntestedStates(body.state);
    assertEq({enabled: true, readOnly: true},
             body.state);

    var contentChildren = body.children();
    assertEq(3, contentChildren.length);
    var okButton = contentChildren[0];
    assertEq('Ok', okButton.attributes['name']);
    RemoveUntestedStates(okButton.state);
    assertEq({enabled: true, focusable: true, readOnly: true},
             okButton.state);
    var userNameInput = contentChildren[1];
    assertEq('Username',
             userNameInput.attributes['description']);
    RemoveUntestedStates(userNameInput.state);
    assertEq({enabled: true, focusable: true},
             userNameInput.state);
    var cancelButton = contentChildren[2];
    assertEq('Cancel',
             cancelButton.attributes['name']);
    RemoveUntestedStates(cancelButton.state);
    assertEq({enabled: true, focusable: true, readOnly: true},
             cancelButton.state);

    // Traversal.
    assertEq(undefined, tree.root.parent());
    assertEq(tree.root, body.parent());

    assertEq(body, tree.root.firstChild());
    assertEq(body, tree.root.lastChild());

    assertEq(okButton, body.firstChild());
    assertEq(cancelButton, body.lastChild());

    assertEq(body, okButton.parent());
    assertEq(body, userNameInput.parent());
    assertEq(body, cancelButton.parent());

    assertEq(undefined, okButton.previousSibling());
    assertEq(undefined, okButton.firstChild());
    assertEq(userNameInput, okButton.nextSibling());
    assertEq(undefined, okButton.lastChild());

    assertEq(okButton, userNameInput.previousSibling());
    assertEq(cancelButton, userNameInput.nextSibling());

    assertEq(userNameInput, cancelButton.previousSibling());
    assertEq(undefined, cancelButton.nextSibling());

    chrome.test.succeed();
  }
];

setUpAndRunTests(allTests);
