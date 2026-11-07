// SPDX-License-Identifier: AGPL-3.0-only

package httpapi

import (
	"crypto/rand"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/invite"
)

func newTestMux() *http.ServeMux {
	return NewMux(Config{
		Invites:     invite.NewRegistry(time.Now, rand.Reader),
		DownloadURL: "https://example.com/download",
	})
}

func TestHealthz(t *testing.T) {
	rec := httptest.NewRecorder()
	newTestMux().ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/healthz", nil))

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	if got, want := rec.Body.String(), "ok "+Version+"\n"; got != want {
		t.Fatalf("body = %q, want %q", got, want)
	}
}

func TestUnknownPathIsNotFound(t *testing.T) {
	for _, path := range []string{"/nope", "/join/join.html", "/join/static/join.js"} {
		rec := httptest.NewRecorder()
		newTestMux().ServeHTTP(rec, httptest.NewRequest(http.MethodGet, path, nil))
		if rec.Code != http.StatusNotFound {
			t.Fatalf("GET %s status = %d, want 404", path, rec.Code)
		}
	}
}
