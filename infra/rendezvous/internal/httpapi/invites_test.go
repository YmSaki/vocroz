// SPDX-License-Identifier: AGPL-3.0-only

package httpapi

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func post(t *testing.T, mux http.Handler, path string, body any) *httptest.ResponseRecorder {
	t.Helper()
	var buf bytes.Buffer
	if s, ok := body.(string); ok {
		buf.WriteString(s)
	} else if err := json.NewEncoder(&buf).Encode(body); err != nil {
		t.Fatal(err)
	}
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, httptest.NewRequest(http.MethodPost, path, &buf))
	return rec
}

func decode[T any](t *testing.T, rec *httptest.ResponseRecorder) T {
	t.Helper()
	var v T
	if err := json.Unmarshal(rec.Body.Bytes(), &v); err != nil {
		t.Fatalf("decode %q: %v", rec.Body.String(), err)
	}
	return v
}

func validRegisterRequest() registerRequest {
	return registerRequest{
		HostPublicKey: b64.EncodeToString(bytes.Repeat([]byte{1}, 32)),
		InviteToken:   b64.EncodeToString(bytes.Repeat([]byte{2}, 16)),
		RendezvousID:  b64.EncodeToString(bytes.Repeat([]byte{3}, 16)),
	}
}

func TestInviteLifecycle(t *testing.T) {
	mux := newTestMux()
	req := validRegisterRequest()

	rec := post(t, mux, "/v1/invites", req)
	if rec.Code != http.StatusCreated {
		t.Fatalf("register status = %d, body %s", rec.Code, rec.Body)
	}
	if got := rec.Header().Get("Cache-Control"); got != "no-store" {
		t.Fatalf("Cache-Control = %q", got)
	}
	issued := decode[registerResponse](t, rec)
	if len(issued.Code) != 10 || issued.DisplayCode[5] != '-' || issued.RevokeKey == "" {
		t.Fatalf("register response = %+v", issued)
	}

	// 人が入力した形（小文字、ハイフン付き）でも解決できる。
	rec = post(t, mux, "/v1/invites/resolve", resolveRequest{Code: strings.ToLower(issued.DisplayCode)})
	if rec.Code != http.StatusOK {
		t.Fatalf("resolve status = %d, body %s", rec.Code, rec.Body)
	}
	resolved := decode[resolveResponse](t, rec)
	if resolved.HostPublicKey != req.HostPublicKey || resolved.InviteToken != req.InviteToken ||
		resolved.RendezvousID != req.RendezvousID || !resolved.ExpiresAt.Equal(issued.ExpiresAt) {
		t.Fatalf("resolve response = %+v, want fields of %+v", resolved, req)
	}

	rec = post(t, mux, "/v1/invites/revoke", revokeRequest{Code: issued.Code, RevokeKey: b64.EncodeToString(make([]byte, 32))})
	if rec.Code != http.StatusForbidden {
		t.Fatalf("revoke with wrong key status = %d", rec.Code)
	}
	rec = post(t, mux, "/v1/invites/revoke", revokeRequest{Code: issued.Code, RevokeKey: issued.RevokeKey})
	if rec.Code != http.StatusNoContent {
		t.Fatalf("revoke status = %d, body %s", rec.Code, rec.Body)
	}

	rec = post(t, mux, "/v1/invites/resolve", resolveRequest{Code: issued.Code})
	if rec.Code != http.StatusGone || decode[errorResponse](t, rec).Error != errInviteRevoked {
		t.Fatalf("resolve after revoke = %d %s", rec.Code, rec.Body)
	}
}

func TestRegisterRejectsInvalidRequests(t *testing.T) {
	mux := newTestMux()
	shortKey := validRegisterRequest()
	shortKey.HostPublicKey = b64.EncodeToString(make([]byte, 31))
	shortToken := validRegisterRequest()
	shortToken.InviteToken = b64.EncodeToString(make([]byte, 15))
	badBase64 := validRegisterRequest()
	badBase64.RendezvousID = "***"
	negativeTTL := validRegisterRequest()
	negativeTTL.TTLSeconds = -1

	for name, body := range map[string]any{
		"short key": shortKey, "short token": shortToken, "bad base64": badBase64, "negative ttl": negativeTTL,
		"not json": "{", "two values": "{} {}", "too large": `{"invite_token":"` + strings.Repeat("A", maxRequestBody) + `"}`,
	} {
		rec := post(t, mux, "/v1/invites", body)
		if rec.Code != http.StatusBadRequest || decode[errorResponse](t, rec).Error != errInvalidRequest {
			t.Errorf("%s: status = %d, body %s", name, rec.Code, rec.Body)
		}
	}
}

func TestResolveReportsUnknownAndMalformedCodes(t *testing.T) {
	mux := newTestMux()
	rec := post(t, mux, "/v1/invites/resolve", resolveRequest{Code: "ABCDE-FGHJK"})
	if rec.Code != http.StatusNotFound || decode[errorResponse](t, rec).Error != errInviteNotFound {
		t.Fatalf("unknown code = %d %s", rec.Code, rec.Body)
	}
	rec = post(t, mux, "/v1/invites/resolve", resolveRequest{Code: "nope"})
	if rec.Code != http.StatusBadRequest || decode[errorResponse](t, rec).Error != errInvalidCode {
		t.Fatalf("malformed code = %d %s", rec.Code, rec.Body)
	}
}

func TestInviteEndpointsRejectGet(t *testing.T) {
	rec := httptest.NewRecorder()
	newTestMux().ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/v1/invites/resolve?code=ABCDEFGHJK", nil))
	if rec.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d, want 405", rec.Code)
	}
}
