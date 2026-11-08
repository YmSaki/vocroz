// SPDX-License-Identifier: AGPL-3.0-only

package httpapi

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestJoinPageEmbedsDownloadLinkAndSecurityHeaders(t *testing.T) {
	rec := httptest.NewRecorder()
	newTestMux().ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/join", nil))

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}
	body := rec.Body.String()
	if !strings.Contains(body, `href="https://example.com/download"`) || !strings.Contains(body, `src="/join/join.js"`) {
		t.Fatalf("page does not link the download URL and script:\n%s", body)
	}
	for _, h := range []string{"Content-Security-Policy", "Referrer-Policy", "X-Content-Type-Options"} {
		if rec.Header().Get(h) == "" {
			t.Fatalf("missing %s header", h)
		}
	}
}

func TestJoinAssetsAreServed(t *testing.T) {
	for path, contentType := range map[string]string{"/join/join.js": "javascript", "/join/join.css": "text/css"} {
		rec := httptest.NewRecorder()
		newTestMux().ServeHTTP(rec, httptest.NewRequest(http.MethodGet, path, nil))
		if rec.Code != http.StatusOK || !strings.Contains(rec.Header().Get("Content-Type"), contentType) {
			t.Fatalf("GET %s = %d %q", path, rec.Code, rec.Header().Get("Content-Type"))
		}
	}
}

func TestJoinPageEscapesDownloadURL(t *testing.T) {
	page := newJoinPage(`javascript:alert("x")`)
	if strings.Contains(string(page.html), "javascript:alert") {
		t.Fatal("unsafe download URL was embedded as is")
	}
}
