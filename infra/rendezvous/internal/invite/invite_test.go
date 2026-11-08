// SPDX-License-Identifier: AGPL-3.0-only

package invite

import (
	"bytes"
	"crypto/rand"
	"errors"
	"testing"
	"time"
)

// fakeClock はテストで時刻を進めるための時計。
type fakeClock struct{ t time.Time }

func (c *fakeClock) now() time.Time          { return c.t }
func (c *fakeClock) advance(d time.Duration) { c.t = c.t.Add(d) }

func newTestRegistry() (*Registry, *fakeClock) {
	clock := &fakeClock{t: time.Date(2026, 11, 7, 12, 0, 0, 0, time.UTC)}
	return NewRegistry(clock.now, rand.Reader), clock
}

func validRegistration() Registration {
	reg := Registration{Token: bytes.Repeat([]byte{0xAB}, MinTokenLen)}
	reg.HostPublicKey[0] = 1
	reg.RendezvousID[0] = 2
	return reg
}

func TestRegistryResolvesRegisteredInvite(t *testing.T) {
	r, clock := newTestRegistry()
	issued, err := r.Register(validRegistration())
	if err != nil {
		t.Fatal(err)
	}
	if want := clock.t.Add(MaxTTL); !issued.Invite.ExpiresAt.Equal(want) {
		t.Fatalf("ExpiresAt = %v, want %v", issued.Invite.ExpiresAt, want)
	}

	got, err := r.Resolve(issued.Invite.Code)
	if err != nil {
		t.Fatal(err)
	}
	if got.HostPublicKey != issued.Invite.HostPublicKey || got.RendezvousID != issued.Invite.RendezvousID ||
		!bytes.Equal(got.Token, issued.Invite.Token) {
		t.Fatalf("Resolve() = %+v, want %+v", got, issued.Invite)
	}
}

func TestRegistryClampsTTLToMaximum(t *testing.T) {
	r, clock := newTestRegistry()
	reg := validRegistration()
	reg.TTL = 2 * time.Hour
	issued, err := r.Register(reg)
	if err != nil {
		t.Fatal(err)
	}
	if want := clock.t.Add(MaxTTL); !issued.Invite.ExpiresAt.Equal(want) {
		t.Fatalf("ExpiresAt = %v, want %v", issued.Invite.ExpiresAt, want)
	}
}

func TestRegistryRejectsShortToken(t *testing.T) {
	r, _ := newTestRegistry()
	reg := validRegistration()
	reg.Token = reg.Token[:MinTokenLen-1]
	if _, err := r.Register(reg); !errors.Is(err, ErrInvalidInvite) {
		t.Fatalf("Register() error = %v, want ErrInvalidInvite", err)
	}
}

func TestRegistryReportsExpiredInvite(t *testing.T) {
	r, clock := newTestRegistry()
	reg := validRegistration()
	reg.TTL = time.Minute
	issued, _ := r.Register(reg)

	clock.advance(time.Minute)
	if _, err := r.Resolve(issued.Invite.Code); !errors.Is(err, ErrExpired) {
		t.Fatalf("Resolve() error = %v, want ErrExpired", err)
	}
}

func TestRegistryRevokesOnlyWithMatchingKey(t *testing.T) {
	r, _ := newTestRegistry()
	issued, _ := r.Register(validRegistration())

	var wrong [RevokeKeyLen]byte
	if err := r.Revoke(issued.Invite.Code, wrong); !errors.Is(err, ErrForbidden) {
		t.Fatalf("Revoke(wrong key) error = %v, want ErrForbidden", err)
	}
	if _, err := r.Resolve(issued.Invite.Code); err != nil {
		t.Fatalf("Resolve() after rejected revoke error = %v", err)
	}

	if err := r.Revoke(issued.Invite.Code, issued.RevokeKey); err != nil {
		t.Fatal(err)
	}
	if _, err := r.Resolve(issued.Invite.Code); !errors.Is(err, ErrRevoked) {
		t.Fatalf("Resolve() error = %v, want ErrRevoked", err)
	}
	if err := r.Revoke(issued.Invite.Code, issued.RevokeKey); err != nil {
		t.Fatalf("second Revoke() error = %v, want nil", err)
	}
}

func TestRegistryReportsUnknownCode(t *testing.T) {
	r, _ := newTestRegistry()
	if _, err := r.Resolve("ABCDEFGHJK"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("Resolve() error = %v, want ErrNotFound", err)
	}
	if err := r.Revoke("ABCDEFGHJK", [RevokeKeyLen]byte{}); !errors.Is(err, ErrNotFound) {
		t.Fatalf("Revoke() error = %v, want ErrNotFound", err)
	}
}

func TestRegistrySweepKeepsTombstoneUntilRetentionPasses(t *testing.T) {
	r, clock := newTestRegistry()
	reg := validRegistration()
	reg.TTL = time.Minute
	issued, _ := r.Register(reg)

	clock.advance(time.Minute + tombstoneRetention - time.Second)
	r.Sweep()
	if _, err := r.Resolve(issued.Invite.Code); !errors.Is(err, ErrExpired) {
		t.Fatalf("Resolve() before retention error = %v, want ErrExpired", err)
	}

	clock.advance(time.Second)
	r.Sweep()
	if _, err := r.Resolve(issued.Invite.Code); !errors.Is(err, ErrNotFound) {
		t.Fatalf("Resolve() after retention error = %v, want ErrNotFound", err)
	}
}

func TestRegistryResolveReturnsIndependentCopy(t *testing.T) {
	r, _ := newTestRegistry()
	issued, _ := r.Register(validRegistration())
	got, _ := r.Resolve(issued.Invite.Code)
	got.Token[0] ^= 0xFF

	again, _ := r.Resolve(issued.Invite.Code)
	if again.Token[0] != 0xAB {
		t.Fatal("Resolve() exposed the stored token slice")
	}
}
