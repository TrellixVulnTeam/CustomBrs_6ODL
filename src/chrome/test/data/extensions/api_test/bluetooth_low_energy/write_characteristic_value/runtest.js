// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function testWriteCharacteristicValue() {
  chrome.test.assertTrue(characteristic != null, '\'characteristic\' is null');
  chrome.test.assertEq(charId, characteristic.instanceId);

  chrome.test.assertEq(writeValue.byteLength, characteristic.value.byteLength);

  var receivedValueBytes = new Uint8Array(characteristic.value);
  for (var i = 0; i < writeValue.byteLength; i++) {
    chrome.test.assertEq(valueBytes[i], receivedValueBytes[i]);
  }

  chrome.test.succeed();
}

var writeCharacteristicValue =
    chrome.bluetoothLowEnergy.writeCharacteristicValue;

var charId = 'char_id0';
var badCharId = 'char_id1';

var characteristic = null;
var bytes = [0x43, 0x68, 0x72, 0x6F, 0x6D, 0x65];
var writeValue = new ArrayBuffer(bytes.length);
var valueBytes = new Uint8Array(writeValue);
valueBytes.set(bytes);

// 1. Unknown characteristic instanceId.
writeCharacteristicValue(badCharId, writeValue, function (result) {
  if (result || !chrome.runtime.lastError) {
    chrome.test.fail('badCharId did not cause failure');
  }

  // 2. Known characteristic instanceId, but call failure.
  writeCharacteristicValue(charId, writeValue, function (result) {
    if (result || !chrome.runtime.lastError) {
      chrome.test.fail('writeCharacteristicValue should have failed');
    }

    // 3. Call should succeed.
    writeCharacteristicValue(charId, writeValue, function (result) {
      if (chrome.runtime.lastError) {
        chrome.test.fail(chrome.runtime.lastError.message);
      }

      chrome.bluetoothLowEnergy.getCharacteristic(charId, function (result) {
        characteristic = result;

        chrome.test.sendMessage('ready', function (message) {
          chrome.test.runTests([testWriteCharacteristicValue]);
        });
      });
    });
  });
});
