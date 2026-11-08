// SPDX-License-Identifier: AGPL-3.0-only

package udpapi

import (
	"net/netip"
	"reflect"
	"testing"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
)

func sampleRegister() Register {
	return Register{
		TxID:         TxID{1, 2, 3, 4, 5, 6, 7, 8},
		RendezvousID: signaling.ID{0x11},
		PeerID:       signaling.ID{0x22},
		Role:         signaling.RoleClient,
		Candidates: []netip.AddrPort{
			netip.MustParseAddrPort("192.168.0.2:50000"),
			netip.MustParseAddrPort("[2001:db8::2]:50000"),
		},
	}
}

func TestRegisterRoundTrips(t *testing.T) {
	want := sampleRegister()
	got, err := ParseRegister(want.Append(nil))
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("ParseRegister() = %+v, want %+v", got, want)
	}
}

func TestRegisteredAndPunchRoundTrip(t *testing.T) {
	ack := Registered{TxID: TxID{9}, Status: StatusCapacity, PeerPresent: true, Reflexive: netip.MustParseAddrPort("203.0.113.1:4000")}
	if got, err := ParseRegistered(ack.Append(nil)); err != nil || got != ack {
		t.Fatalf("ParseRegistered() = %+v, %v; want %+v", got, err, ack)
	}

	punch := Punch{TxID: TxID{7}, RendezvousID: signaling.ID{1}, PeerID: signaling.ID{2}, PeerRole: signaling.RoleHost,
		Candidates: []netip.AddrPort{netip.MustParseAddrPort("203.0.113.1:4000")}}
	if got, err := ParsePunch(punch.Append(nil)); err != nil || !reflect.DeepEqual(got, punch) {
		t.Fatalf("ParsePunch() = %+v, %v; want %+v", got, err, punch)
	}
}

func TestParseRegisterRejectsMalformedMessages(t *testing.T) {
	valid := sampleRegister().Append(nil)
	mutate := func(f func(b []byte) []byte) []byte { return f(append([]byte(nil), valid...)) }

	tooMany := sampleRegister()
	for range signaling.MaxLocalCandidates {
		tooMany.Candidates = append(tooMany.Candidates, netip.MustParseAddrPort("10.0.0.1:1"))
	}
	unspecified := sampleRegister()
	unspecified.Candidates = []netip.AddrPort{netip.MustParseAddrPort("0.0.0.0:5000")}
	zeroPort := sampleRegister()
	zeroPort.Candidates = []netip.AddrPort{netip.MustParseAddrPort("10.0.0.1:0")}

	cases := map[string][]byte{
		"truncated":      valid[:len(valid)-1],
		"trailing byte":  append(append([]byte(nil), valid...), 0),
		"bad version":    mutate(func(b []byte) []byte { b[3] = 2; return b }),
		"wrong type":     mutate(func(b []byte) []byte { b[4] = byte(TypePunch); return b }),
		"bad role":       mutate(func(b []byte) []byte { b[headerLen+32] = 9; return b }),
		"bad family":     mutate(func(b []byte) []byte { b[headerLen+34] = 5; return b }),
		"too many":       tooMany.Append(nil),
		"unspecified":    unspecified.Append(nil),
		"zero port":      zeroPort.Append(nil),
		"not rendezvous": []byte("hello, world!!"),
	}
	for name, b := range cases {
		if _, err := ParseRegister(b); err == nil {
			t.Errorf("ParseRegister(%s) error = nil", name)
		}
	}
}
