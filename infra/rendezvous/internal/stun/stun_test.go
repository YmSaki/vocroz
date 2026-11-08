// SPDX-License-Identifier: AGPL-3.0-only

package stun

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"net/netip"
	"strings"
	"testing"
)

// rfc5769TxID は RFC 5769（STUN のテストベクター）の応答例のトランザクション ID。
var rfc5769TxID = [12]byte{0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae}

func bindingRequest(txID [12]byte, attrs ...[]byte) []byte {
	msg := newHeader(typeBindingRequest, txID)
	for _, a := range attrs {
		msg = appendAttribute(msg, binary.BigEndian.Uint16(a[0:2]), a[2:])
	}
	return msg
}

func mustHex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(strings.Join(strings.Fields(s), ""))
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestRespondEncodesXorMappedAddressLikeRFC5769(t *testing.T) {
	tests := []struct {
		from netip.AddrPort
		want string
	}{
		// RFC 5769 2.2（IPv4）と 2.3（IPv6）の XOR-MAPPED-ADDRESS の値。
		{netip.MustParseAddrPort("192.0.2.1:32853"), "0001 a147 e112a643"},
		{netip.MustParseAddrPort("[2001:db8:1234:5678:11:2233:4455:6677]:32853"), "0002 a147 0113a9fa a5d3f179 bc25f4b5 bed2b9d9"},
		// デュアルスタックのソケットで受けた IPv4 射影アドレスは IPv4 として返す。
		{netip.MustParseAddrPort("[::ffff:192.0.2.1]:32853"), "0001 a147 e112a643"},
	}
	for _, tt := range tests {
		resp, ok := Respond(bindingRequest(rfc5769TxID), tt.from)
		if !ok {
			t.Fatalf("Respond(%v) ok = false", tt.from)
		}
		if got := binary.BigEndian.Uint16(resp[0:2]); got != typeBindingSuccess {
			t.Fatalf("type = %#04x, want success", got)
		}
		want := mustHex(t, tt.want)
		if got := resp[headerLen+4 : headerLen+4+len(want)]; !bytes.Equal(got, want) {
			t.Fatalf("XOR-MAPPED-ADDRESS for %v = %x, want %x", tt.from, got, want)
		}
		if !bytes.Equal(resp[8:20], rfc5769TxID[:]) {
			t.Fatal("transaction ID is not echoed")
		}
		if _, ok := scanAttributes(resp); !ok {
			t.Fatal("response has an invalid FINGERPRINT")
		}
	}
}

func TestRespondValidatesFingerprintOfRFC5769SampleRequest(t *testing.T) {
	// RFC 5769 2.1 のリクエスト。ICE の PRIORITY（必須属性）を含むので、420 が返る。
	req := mustHex(t, `
		000100582112a442b7e7a701bc34d686fa87dfae
		80220010 5354554e 20746573 7420636c 69656e74
		00240004 6e0001ff
		80290008 932ff9b151263b36
		00060009 6576746a3a68367659202020
		00080014 9aeaa70cbfd8cb56781ef2b5b2d3f249c1b571a2
		80280004 e57a3bcf`)
	resp, ok := Respond(req, netip.MustParseAddrPort("192.0.2.1:32853"))
	if !ok {
		t.Fatal("Respond() ok = false for the RFC 5769 sample")
	}
	if got := binary.BigEndian.Uint16(resp[0:2]); got != typeBindingError {
		t.Fatalf("type = %#04x, want error", got)
	}
	if !bytes.Contains(resp, []byte{0x00, 0x0A, 0x00, 0x02, 0x00, 0x24}) {
		t.Fatalf("UNKNOWN-ATTRIBUTES does not list PRIORITY: %x", resp)
	}

	req[len(req)-1] ^= 0xFF
	if _, ok := Respond(req, netip.MustParseAddrPort("192.0.2.1:32853")); ok {
		t.Fatal("Respond() accepted a request with a broken FINGERPRINT")
	}
}

func TestRespondIgnoresNonRequestsAndMalformedMessages(t *testing.T) {
	from := netip.MustParseAddrPort("192.0.2.1:1")
	valid := bindingRequest(rfc5769TxID)

	indication := bytes.Clone(valid)
	binary.BigEndian.PutUint16(indication[0:2], 0x0011)
	truncated := bindingRequest(rfc5769TxID, []byte{0x80, 0x22, 'a', 'b', 'c', 'd'})[:headerLen+6]
	binary.BigEndian.PutUint16(truncated[2:4], 8)
	badLength := bytes.Clone(valid)
	binary.BigEndian.PutUint16(badLength[2:4], 4)

	for name, msg := range map[string][]byte{
		"indication": indication, "truncated": truncated, "bad length": badLength, "short": valid[:19],
	} {
		if _, ok := Respond(msg, from); ok {
			t.Fatalf("Respond(%s) ok = true", name)
		}
	}
}

func TestRespondAcceptsAuthAttributesWithoutVerifying(t *testing.T) {
	req := bindingRequest(rfc5769TxID, []byte{0x00, 0x06, 'u', 's', 'e', 'r'})
	resp, ok := Respond(req, netip.MustParseAddrPort("192.0.2.1:1"))
	if !ok || binary.BigEndian.Uint16(resp[0:2]) != typeBindingSuccess {
		t.Fatalf("Respond() = %x, %v; want success", resp, ok)
	}
}

func TestIsMessageDistinguishesStunFromOtherPackets(t *testing.T) {
	if !IsMessage(bindingRequest(rfc5769TxID)) {
		t.Fatal("IsMessage(binding request) = false")
	}
	if IsMessage([]byte("VRZ\x01 not a stun message")) {
		t.Fatal("IsMessage(rendezvous message) = true")
	}
}
