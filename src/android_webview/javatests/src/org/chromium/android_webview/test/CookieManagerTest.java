// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.test.MoreAsserts;
import android.test.suitebuilder.annotation.MediumTest;
import android.util.Pair;
import android.webkit.ValueCallback;

import static org.chromium.android_webview.test.util.CookieUtils.TestValueCallback;

import org.chromium.android_webview.AwContents;
import org.chromium.android_webview.AwCookieManager;
import org.chromium.android_webview.test.util.CookieUtils;
import org.chromium.android_webview.test.util.JSUtils;
import org.chromium.base.test.util.Feature;
import org.chromium.net.test.util.TestWebServer;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.Callable;

/**
 * Tests for the CookieManager.
 */
public class CookieManagerTest extends AwTestBase {

    private AwCookieManager mCookieManager;
    private TestAwContentsClient mContentsClient;
    private AwContents mAwContents;

    @Override
    protected void setUp() throws Exception {
        super.setUp();

        mCookieManager = new AwCookieManager();
        mContentsClient = new TestAwContentsClient();
        final AwTestContainerView testContainerView =
                createAwTestContainerViewOnMainSync(mContentsClient);
        mAwContents = testContainerView.getAwContents();
        mAwContents.getSettings().setJavaScriptEnabled(true);
        assertNotNull(mCookieManager);

        // All tests start with no cookies.
        try {
            clearCookies();
        } catch (Throwable e) {
            throw new RuntimeException("Could not clear cookies.");
        }

        // But setting cookies enabled.
        mCookieManager.setAcceptCookie(true);
        assertTrue(mCookieManager.acceptCookie());
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testAcceptCookie() throws Throwable {
        TestWebServer webServer = null;
        try {
            webServer = new TestWebServer(false);
            String path = "/cookie_test.html";
            String responseStr =
                    "<html><head><title>TEST!</title></head><body>HELLO!</body></html>";
            String url = webServer.setResponse(path, responseStr, null);

            mCookieManager.setAcceptCookie(false);
            assertFalse(mCookieManager.acceptCookie());

            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            setCookieWithJavaScript("test1", "value1");
            assertNull(mCookieManager.getCookie(url));

            List<Pair<String, String>> responseHeaders = new ArrayList<Pair<String, String>>();
            responseHeaders.add(
                    Pair.create("Set-Cookie", "header-test1=header-value1; path=" + path));
            url = webServer.setResponse(path, responseStr, responseHeaders);
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            assertNull(mCookieManager.getCookie(url));

            mCookieManager.setAcceptCookie(true);
            assertTrue(mCookieManager.acceptCookie());

            url = webServer.setResponse(path, responseStr, null);
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            setCookieWithJavaScript("test2", "value2");
            waitForCookie(url);
            String cookie = mCookieManager.getCookie(url);
            assertNotNull(cookie);
            validateCookies(cookie, "test2");

            responseHeaders = new ArrayList<Pair<String, String>>();
            responseHeaders.add(
                    Pair.create("Set-Cookie", "header-test2=header-value2 path=" + path));
            url = webServer.setResponse(path, responseStr, responseHeaders);
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            waitForCookie(url);
            cookie = mCookieManager.getCookie(url);
            assertNotNull(cookie);
            validateCookies(cookie, "test2", "header-test2");
        } finally {
            if (webServer != null) webServer.shutdown();
        }
    }

    private void setCookieWithJavaScript(final String name, final String value)
            throws Throwable {
        JSUtils.executeJavaScriptAndWaitForResult(
                this, mAwContents,
                mContentsClient.getOnEvaluateJavaScriptResultHelper(),
                "var expirationDate = new Date();" +
                "expirationDate.setDate(expirationDate.getDate() + 5);" +
                "document.cookie='" + name + "=" + value +
                        "; expires=' + expirationDate.toUTCString();");
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testRemoveAllCookies() throws Exception {
        mCookieManager.setCookie("http://www.example.com", "name=test");
        assertTrue(mCookieManager.hasCookies());
        mCookieManager.removeAllCookies();
        assertFalse(mCookieManager.hasCookies());
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testRemoveSessionCookies() throws Exception {
        final String url = "http://www.example.com";
        final String sessionCookie = "cookie1=peter";
        final String normalCookie = "cookie2=sue";

        mCookieManager.setCookie(url, sessionCookie);
        mCookieManager.setCookie(url, makeExpiringCookie(normalCookie, 600));

        mCookieManager.removeSessionCookies();

        String allCookies = mCookieManager.getCookie(url);
        assertFalse(allCookies.contains(sessionCookie));
        assertTrue(allCookies.contains(normalCookie));
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testSetCookie() throws Throwable {
        String url = "http://www.example.com";
        String cookie = "name=test";
        mCookieManager.setCookie(url, cookie);
        assertEquals(cookie, mCookieManager.getCookie(url));
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testHasCookie() throws Throwable {
        assertFalse(mCookieManager.hasCookies());
        mCookieManager.setCookie("http://www.example.com", "name=test");
        assertTrue(mCookieManager.hasCookies());
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testSetCookieCallback() throws Throwable {
        final String url = "http://www.example.com";
        final String cookie = "name=test";
        final String brokenUrl = "foo";

        final TestValueCallback<Boolean> callback = new TestValueCallback<Boolean>();
        int callCount = callback.getOnReceiveValueHelper().getCallCount();

        setCookieOnUiThread(url, cookie, callback);
        callback.getOnReceiveValueHelper().waitForCallback(callCount);
        assertTrue(callback.getValue());
        assertEquals(cookie, mCookieManager.getCookie(url));

        callCount = callback.getOnReceiveValueHelper().getCallCount();

        setCookieOnUiThread(brokenUrl, cookie, callback);
        callback.getOnReceiveValueHelper().waitForCallback(callCount);
        assertFalse(callback.getValue());
        assertEquals(null, mCookieManager.getCookie(brokenUrl));
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testSetCookieNullCallback() throws Throwable {
        mCookieManager.setAcceptCookie(true);
        assertTrue(mCookieManager.acceptCookie());

        final String url = "http://www.example.com";
        final String cookie = "name=test";

        mCookieManager.setCookie(url, cookie, null);

        poll(new Callable<Boolean>() {
            @Override
            public Boolean call() throws Exception {
                return mCookieManager.hasCookies();
            }
        });
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testRemoveAllCookiesCallback() throws Throwable {
        TestValueCallback<Boolean> callback = new TestValueCallback<Boolean>();
        int callCount = callback.getOnReceiveValueHelper().getCallCount();

        mCookieManager.setCookie("http://www.example.com", "name=test");

        // When we remove all cookies the first time some cookies are removed.
        removeAllCookiesOnUiThread(callback);
        callback.getOnReceiveValueHelper().waitForCallback(callCount);
        assertTrue(callback.getValue());
        assertFalse(mCookieManager.hasCookies());

        callCount = callback.getOnReceiveValueHelper().getCallCount();

        // The second time none are removed.
        removeAllCookiesOnUiThread(callback);
        callback.getOnReceiveValueHelper().waitForCallback(callCount);
        assertFalse(callback.getValue());
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testRemoveAllCookiesNullCallback() throws Throwable {
        mCookieManager.setCookie("http://www.example.com", "name=test");

        mCookieManager.removeAllCookies(null);

        // Eventually the cookies are removed.
        poll(new Callable<Boolean>() {
            @Override
            public Boolean call() throws Exception {
                return !mCookieManager.hasCookies();
            }
        });
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testRemoveSessionCookiesCallback() throws Throwable {
        final String url = "http://www.example.com";
        final String sessionCookie = "cookie1=peter";
        final String normalCookie = "cookie2=sue";

        TestValueCallback<Boolean> callback = new TestValueCallback<Boolean>();
        int callCount = callback.getOnReceiveValueHelper().getCallCount();

        mCookieManager.setCookie(url, sessionCookie);
        mCookieManager.setCookie(url, makeExpiringCookie(normalCookie, 600));

        // When there is a session cookie then it is removed.
        removeSessionCookiesOnUiThread(callback);
        callback.getOnReceiveValueHelper().waitForCallback(callCount);
        assertTrue(callback.getValue());
        String allCookies = mCookieManager.getCookie(url);
        assertTrue(!allCookies.contains(sessionCookie));
        assertTrue(allCookies.contains(normalCookie));

        callCount = callback.getOnReceiveValueHelper().getCallCount();

        // If there are no session cookies then none are removed.
        removeSessionCookiesOnUiThread(callback);
        callback.getOnReceiveValueHelper().waitForCallback(callCount);
        assertFalse(callback.getValue());
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testRemoveSessionCookiesNullCallback() throws Throwable {
        final String url = "http://www.example.com";
        final String sessionCookie = "cookie1=peter";
        final String normalCookie = "cookie2=sue";

        mCookieManager.setCookie(url, sessionCookie);
        mCookieManager.setCookie(url, makeExpiringCookie(normalCookie, 600));
        String allCookies = mCookieManager.getCookie(url);
        assertTrue(allCookies.contains(sessionCookie));
        assertTrue(allCookies.contains(normalCookie));

        mCookieManager.removeSessionCookies(null);

        // Eventually the session cookie is removed.
        poll(new Callable<Boolean>() {
            @Override
            public Boolean call() throws Exception {
                String c = mCookieManager.getCookie(url);
                return !c.contains(sessionCookie) && c.contains(normalCookie);
            }
        });
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testExpiredCookiesAreNotSet() throws Exception {
        final String url = "http://www.example.com";
        final String cookie = "cookie1=peter";

        mCookieManager.setCookie(url, makeExpiringCookie(cookie, -1));
        assertNull(mCookieManager.getCookie(url));
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testCookiesExpire() throws Exception {
        final String url = "http://www.example.com";
        final String cookie = "cookie1=peter";

        mCookieManager.setCookie(url, makeExpiringCookieMs(cookie, 1200));

        // The cookie exists:
        assertTrue(mCookieManager.hasCookies());

        // But eventually expires:
        poll(new Callable<Boolean>() {
            @Override
            public Boolean call() throws Exception {
                return !mCookieManager.hasCookies();
            }
        });
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testCookieExpiration() throws Exception {
        final String url = "http://www.example.com";
        final String sessionCookie = "cookie1=peter";
        final String longCookie = "cookie2=marc";

        mCookieManager.setCookie(url, sessionCookie);
        mCookieManager.setCookie(url, makeExpiringCookie(longCookie, 600));

        String allCookies = mCookieManager.getCookie(url);
        assertTrue(allCookies.contains(sessionCookie));
        assertTrue(allCookies.contains(longCookie));

        // Removing expired cookies doesn't have an observable effect but since people will still
        // be calling it for a while it shouldn't break anything either.
        mCookieManager.removeExpiredCookies();

        allCookies = mCookieManager.getCookie(url);
        assertTrue(allCookies.contains(sessionCookie));
        assertTrue(allCookies.contains(longCookie));
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testThirdPartyCookie() throws Throwable {
        TestWebServer webServer = null;
        try {
            // In theory we need two servers to test this, one server ('the first party')
            // which returns a response with a link to a second server ('the third party')
            // at different origin. This second server attempts to set a cookie which should
            // fail if AcceptThirdPartyCookie() is false.
            // Strictly according to the letter of RFC6454 it should be possible to set this
            // situation up with two TestServers on different ports (these count as having
            // different origins) but Chrome is not strict about this and does not check the
            // port. Instead we cheat making some of the urls come from localhost and some
            // from 127.0.0.1 which count (both in theory and pratice) as having different
            // origins.
            webServer = new TestWebServer(false);

            // Turn global allow on.
            mCookieManager.setAcceptCookie(true);
            assertTrue(mCookieManager.acceptCookie());

            // When third party cookies are disabled...
            mCookieManager.setAcceptThirdPartyCookie(false);
            assertFalse(mCookieManager.acceptThirdPartyCookie());

            // ...we can't set third party cookies.
            // First on the third party server we create a url which tries to set a cookie.
            String cookieUrl = toThirdPartyUrl(
                    makeCookieUrl(webServer, "/cookie_1.js", "test1", "value1"));
            // Then we create a url on the first party server which links to the first url.
            String url = makeScriptLinkUrl(webServer, "/content_1.html", cookieUrl);
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            assertNull(mCookieManager.getCookie(cookieUrl));

            // When third party cookies are enabled...
            mCookieManager.setAcceptThirdPartyCookie(true);
            assertTrue(mCookieManager.acceptThirdPartyCookie());

            // ...we can set third party cookies.
            cookieUrl = toThirdPartyUrl(
                    makeCookieUrl(webServer, "/cookie_2.js", "test2", "value2"));
            url = makeScriptLinkUrl(webServer, "/content_2.html", cookieUrl);
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            waitForCookie(cookieUrl);
            String cookie = mCookieManager.getCookie(cookieUrl);
            assertNotNull(cookie);
            validateCookies(cookie, "test2");
        } finally {
            if (webServer != null) webServer.shutdown();
        }
    }

    /**
     * Creates a response on the TestWebServer which attempts to set a cookie when fetched.
     * @param  webServer  the webServer on which to create the response
     * @param  path the path component of the url (e.g "/cookie_test.html")
     * @param  key the key of the cookie
     * @param  value the value of the cookie
     * @return  the url which gets the response
     */
    private String makeCookieUrl(TestWebServer webServer, String path, String key, String value) {
        String response = "";
        List<Pair<String, String>> responseHeaders = new ArrayList<Pair<String, String>>();
        responseHeaders.add(
            Pair.create("Set-Cookie", key + "=" + value + "; path=" + path));
        return webServer.setResponse(path, response, responseHeaders);
    }

    /**
     * Creates a response on the TestWebServer which contains a script tag with an external src.
     * @param  webServer  the webServer on which to create the response
     * @param  path the path component of the url (e.g "/my_thing_with_script.html")
     * @param  url the url which which should appear as the src of the script tag.
     * @return  the url which gets the response
     */
    private String makeScriptLinkUrl(TestWebServer webServer, String path, String url) {
        String responseStr = "<html><head><title>Content!</title></head>" +
                    "<body><script src=" + url + "></script></body></html>";
        return webServer.setResponse(path, responseStr, null);
    }

    @MediumTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testThirdPartyJavascriptCookie() throws Throwable {
        TestWebServer webServer = null;
        try {
            // This test again uses 127.0.0.1/localhost trick to simulate a third party.
            webServer = new TestWebServer(false);

            mCookieManager.setAcceptCookie(true);
            assertTrue(mCookieManager.acceptCookie());

            // When third party cookies are disabled...
            mCookieManager.setAcceptThirdPartyCookie(false);
            assertFalse(mCookieManager.acceptThirdPartyCookie());

            // ...we can't set third party cookies.
            // We create a script which tries to set a cookie on a third party.
            String cookieUrl = toThirdPartyUrl(
                    makeCookieScriptUrl(webServer, "/cookie_1.html", "test1", "value1"));
            // Then we load it as an iframe.
            String url = makeIframeUrl(webServer, "/content_1.html", cookieUrl);
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            assertNull(mCookieManager.getCookie(cookieUrl));

            // When third party cookies are enabled...
            mCookieManager.setAcceptThirdPartyCookie(true);
            assertTrue(mCookieManager.acceptThirdPartyCookie());

            // ...we can set third party cookies.
            cookieUrl = toThirdPartyUrl(
                    makeCookieScriptUrl(webServer, "/cookie_2.html", "test2", "value2"));
            url = makeIframeUrl(webServer, "/content_2.html", cookieUrl);
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            String cookie = mCookieManager.getCookie(cookieUrl);
            assertNotNull(cookie);
            validateCookies(cookie, "test2");
        } finally {
            if (webServer != null) webServer.shutdown();
        }
    }

    /**
     * Creates a response on the TestWebServer which attempts to set a cookie when fetched.
     * @param  webServer  the webServer on which to create the response
     * @param  path the path component of the url (e.g "/my_thing_with_iframe.html")
     * @param  url the url which which should appear as the src of the iframe.
     * @return  the url which gets the response
     */
    private String makeIframeUrl(TestWebServer webServer, String path, String url) {
        String responseStr = "<html><head><title>Content!</title></head>" +
                    "<body><iframe src=" + url + "></iframe></body></html>";
        return webServer.setResponse(path, responseStr, null);
    }

    /**
     * Creates a response on the TestWebServer with a script that attempts to set a cookie.
     * @param  webServer  the webServer on which to create the response
     * @param  path the path component of the url (e.g "/cookie_test.html")
     * @param  key the key of the cookie
     * @param  value the value of the cookie
     * @return  the url which gets the response
     */
    private String makeCookieScriptUrl(TestWebServer webServer, String path, String key,
            String value) {
        String response = "<html><head></head><body>" +
            "<script>document.cookie = \"" + key + "=" + value + "\";</script></body></html>";
        return webServer.setResponse(path, response, null);
    }

    /**
     * Makes a url look as if it comes from a different host.
     * @param  url the url to fake.
     * @return  the resulting after faking.
     */
    private String toThirdPartyUrl(String url) {
        return url.replace("localhost", "127.0.0.1");
    }

    private void setCookieOnUiThread(final String url, final String cookie,
            final ValueCallback<Boolean> callback) throws Throwable {
        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                mCookieManager.setCookie(url, cookie, callback);
            }
        });
    }

    private void removeSessionCookiesOnUiThread(final ValueCallback<Boolean> callback)
            throws Throwable {
        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                mCookieManager.removeSessionCookies(callback);
            }
        });
    }

    private void removeAllCookiesOnUiThread(final ValueCallback<Boolean> callback)
            throws Throwable {
        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                mCookieManager.removeAllCookies(callback);
            }
        });
    }

    /**
     * Clears all cookies synchronously.
     */
    private void clearCookies() throws Throwable {
        CookieUtils.clearCookies(this, mCookieManager);
    }

    private void waitForCookie(final String url) throws Exception {
        poll(new Callable<Boolean>() {
            @Override
            public Boolean call() throws Exception {
                return mCookieManager.getCookie(url) != null;
            }
        });
    }

    private void validateCookies(String responseCookie, String... expectedCookieNames) {
        String[] cookies = responseCookie.split(";");
        Set<String> foundCookieNames = new HashSet<String>();
        for (String cookie : cookies) {
            foundCookieNames.add(cookie.substring(0, cookie.indexOf("=")).trim());
        }
        MoreAsserts.assertEquals(
                foundCookieNames, new HashSet<String>(Arrays.asList(expectedCookieNames)));
    }

    private String makeExpiringCookie(String cookie, int secondsTillExpiry) {
        return makeExpiringCookieMs(cookie, secondsTillExpiry * 1000);
    }

    @SuppressWarnings("deprecation")
    private String makeExpiringCookieMs(String cookie, int millisecondsTillExpiry) {
        Date date = new Date();
        date.setTime(date.getTime() + millisecondsTillExpiry);
        return cookie + "; expires=" + date.toGMTString();
    }
}
