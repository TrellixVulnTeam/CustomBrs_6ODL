/**
 * Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Counts the number of frames that have been captured. Used in timeout
 * adjustments.
 * @private
 */
var gFrameCounter = 0;

/**
 * The gStartOfTime when the capturing begins. Used for timeout adjustments.
 * @private
 */
var gStartOfTime = 0;

/**
 * The duration of the all frame capture in milliseconds.
 * @private
 */
var gCaptureDuration = 0;

/**
 * The time interval at which the video is sampled.
 * @private
 */
var gFrameCaptureInterval = 0;

/**
 * The global array of frames. Frames are pushed, i.e. this should be treated as
 * a queue and we should read from the start.
 * @private
 */
var gFrames = new Array();

/**
 * The WebSocket connection to the PyWebSocket server.
 * @private
 */
var gWebSocket = null;

/**
 * A flag to show whether the WebSocket is open;
 * @private
 */
var gWebSocketOpened = false;

/**
 * We need to skip the first two frames due to timing issues. This flags helps
 * us determine weather or not to skip them.
 * @private
 */
var gFrameIntervalAdjustment = false;

/**
 * We need this global variable to synchronize with the test how long to run the
 * call between the two peers.
 */
var dDoneFrameCapturing = false;

/**
 * Upon load of the window opens the WebSocket to the PyWebSocket server. The
 * server should already be up and running.
 */
window.onload = function() {
  tryOpeningWebSocket();
}

/**
 * Starts the frame capturing.
 *
 * @param {!Object} The video tag from which the height and width parameters are
                    to be extracted.
 * @param {Number} The frame rate at which we would like to capture frames.
 * @param {Number} The duration of the frame capture in seconds.
 */
function startFrameCapture(videoTag, frame_rate, duration) {
  gFrameCaptureInterval = 1000/frame_rate;
  gCaptureDuration = 1000 * duration;
  var width = videoTag.videoWidth;
  var height = videoTag.videoHeight;

  if (width == 0 || height == 0) {
    throw failTest('Trying to capture from ' + videoTag.id +
                   ' but it is not playing any video.');
  }

  console.log('Received width is: ' + width + ', received height is: ' + height
              + ', capture interval is: ' + gFrameCaptureInterval +
              ', duration is: ' + gCaptureDuration);

  var remoteCanvas = document.createElement('canvas');
  remoteCanvas.width = width;
  remoteCanvas.height = height;
  document.body.appendChild(remoteCanvas);

  gStartOfTime = new Date().getTime();
  setTimeout(function() { shoot(videoTag, remoteCanvas, width, height); },
             gFrameCaptureInterval);
}

/**
 * Captures an image frame from the provided video element.
 *
 * @private
 * @param {Video} video HTML5 video element from where the image frame will
 * be captured.
 * @param {!Object} 2d context of the canvas on which the image frame will be
 * captured.
 * @param {Number} The width of the video/canvas area to be captured.
 * @param {Number} The height of the video/canvas area to be captured.
 *
 * @return {Object} Returns the ImageData object.
 */
function captureFrame_(video, context, width, height) {
  context.drawImage(video, 0, 0, width, height);
  return context.getImageData(0, 0, width, height);
}

/**
 * The function which is called at the end of every gFrameCaptureInterval. Gets
 * the current frame from the video and extracts the data from it. Then it saves
 * it in the frames array and adjusts the capture interval (timers in JavaScript
 * aren't precise).
 *
 * @param {!Object} The video whose frames are to be captured.
 * @param {Canvas} The canvas on which the image will be captured.
 * @param {Number} The width of the video/canvas area to be captured.
 * @param {Number} The height of the video area to be captured.
 */
function shoot(video, canvas, width, height) {
  // The first two captured frames have big difference between the ideal time
  // interval between two frames and the real one. As a consequence this affects
  // enormously the interval adjustment for subsequent frames. That's why we
  // have to reset the time after the first two frames and get rid of these two
  // frames.
  if (gFrameCounter == 1 && !gFrameIntervalAdjustment) {
    gStartOfTime = new Date().getTime();
    gFrameCounter = 0;
    gFrameIntervalAdjustment = true;
    gFrames.pop();
    gFrames.pop();
  }

  // We capture the whole video frame.
  var img = captureFrame_(video, canvas.getContext('2d'), width, height);
  gFrames.push(img.data.buffer);
  gFrameCounter++;

  // Adjust the timer.
  var current_time = new Date().getTime();
  var ideal_time = gFrameCounter*gFrameCaptureInterval;
  var real_time_elapsed = current_time - gStartOfTime;
  var diff = real_time_elapsed - ideal_time;

  if (real_time_elapsed < gCaptureDuration) {
    // If duration isn't over shoot again
    setTimeout(function() { shoot(video, canvas, width, height); },
               gFrameCaptureInterval - diff);
  } else {  // Else reset gFrameCounter and send the frames
    dDoneFrameCapturing = true;
    gFrameCounter = 0;
    clearPage_();
    prepareProgressBar_();
    sendFrames();
  }
}

/**
 * Queries if we're done with the frame capturing yet.
 */
function doneFrameCapturing() {
  if (dDoneFrameCapturing) {
    returnToTest('done-capturing');
  } else {
    returnToTest('still-capturing');
  }
}

/**
 * Send the frames to the remote PyWebSocket server. Use setTimeout to regularly
 * try to send the frames.
 */
function sendFrames() {
  if (!gWebSocketOpened) {
    console.log('WebSocket connection is not yet open');
    setTimeout(function() { sendFrames(); }, 100);
    return;
  }

  progressBar = document.getElementById('progress-bar');
  if (gFrames.length > 0) {
    var frame = gFrames.shift();
    gWebSocket.send(frame);
    gFrameCounter++;
    setTimeout(function() { sendFrames(); }, 100);

    var totalNumFrames = gFrameCounter + gFrames.length;
    progressBar.innerHTML =
        'Writing captured frames to disk: ' +
        '(' + gFrameCounter + '/' + totalNumFrames + ')';
  } else {
    progressBar.innerHTML = 'Finished sending frames.'
    console.log('Finished sending frames.');
  }
}

/**
 * Function checking whether there are more frames to send to the pywebsocket
 * server.
 */
function haveMoreFramesToSend() {
  if (gFrames.length == 0) {
    returnToTest('no-more-frames');
  } else {
    returnToTest('still-have-frames');
  }
}

/**
 * Continuously tries to open a WebSocket to the pywebsocket server.
 */
function tryOpeningWebSocket() {
  if (!gWebSocketOpened) {
    console.log('Once again trying to open web socket');
    openWebSocket();
    setTimeout(function() { tryOpeningWebSocket(); }, 1000);
  }
}

/**
 * Open the WebSocket connection and register some events.
 */
function openWebSocket() {
  if (!gWebSocketOpened) {
    gWebSocket = new WebSocket('ws://localhost:12221/webrtc_write');
  }

  gWebSocket.onopen = function () {
    console.log('Opened WebSocket connection');
    gWebSocketOpened = true;
  };

  gWebSocket.onerror = function (error) {
    console.log('WebSocket Error ' + error);
  };

  gWebSocket.onmessage = function (e) {
    console.log('Server says: ' + e.data);
  };
}

/**
 * @private
 */
function clearPage_() {
  document.body.innerHTML = '';
}

/**
 * @private
 */
function prepareProgressBar_() {
  document.body.innerHTML =
    '<html><body>' +
    '<p id="progress-bar" style="position: absolute; top: 50%; left: 40%;">' +
    'Preparing to send frames.</p>' +
    '</body></html>';
}
