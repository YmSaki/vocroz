// SPDX-License-Identifier: AGPL-3.0-only
//
// マニフェストの組み立てと署名のテスト。

package main

import (
	"crypto/ed25519"
	"encoding/hex"
	"strings"
	"testing"
)

// testSeed は modules/update/tests/manifest_verifier_test.cpp の kGoSignedManifest を
// 作ったときのシード（バイト 0x00〜0x1f）。C++ 側と同じ署名になることを確かめる。
func testSeed() []byte {
	seed := make([]byte, ed25519.SeedSize)
	for i := range seed {
		seed[i] = byte(i)
	}
	return seed
}

func testManifest() Manifest {
	return Manifest{
		Version: "0.1.0-alpha.2",
		URL:     "https://example.invalid/vocroz-0.1.0-alpha.2.zip",
		SHA256:  strings.Repeat("ab", 32),
		Size:    1234,
	}
}

func TestBodyUsesFixedKeyOrder(t *testing.T) {
	body, err := testManifest().Body()
	if err != nil {
		t.Fatal(err)
	}
	want := "format=1\nproduct=vocroz\nversion=0.1.0-alpha.2\n" +
		"url=https://example.invalid/vocroz-0.1.0-alpha.2.zip\n" +
		"sha256=" + strings.Repeat("ab", 32) + "\nsize=1234\npublisher=\n"
	if body != want {
		t.Fatalf("body = %q, want %q", body, want)
	}
}

func TestBodyRejectsPlainHTTP(t *testing.T) {
	m := testManifest()
	m.URL = "http://example.invalid/x.zip"
	if _, err := m.Body(); err == nil {
		t.Fatal("http:// を受け付けた")
	}
}

func TestBodyRejectsNewlineInValue(t *testing.T) {
	m := testManifest()
	m.Publisher = "a\nsize=1"
	if _, err := m.Body(); err == nil {
		t.Fatal("改行を含む値を受け付けた")
	}
}

func TestSignAppendsVerifiableSignature(t *testing.T) {
	key := ed25519.NewKeyFromSeed(testSeed())
	body, err := testManifest().Body()
	if err != nil {
		t.Fatal(err)
	}
	signed := Sign(body, key)
	if !strings.HasPrefix(signed, body) || !strings.HasSuffix(signed, "\n") {
		t.Fatalf("署名の行の位置が正しくない: %q", signed)
	}
	if !ed25519.Verify(key.Public().(ed25519.PublicKey), []byte(body), signatureOf(t, signed)) {
		t.Fatal("署名を検証できない")
	}
	// Ed25519 は決定的なので、C++ のテストに埋め込んだ値と一致し続けることを確かめる。
	const want = "49d614e5f674d3fe7789a9489d590b743f9c6db729892b19e95d0e6a7f96e282" +
		"23fe90a3e722ce5908b7eb2de6d4b7ac6be131f2651b74cebc2d98416b41bb09"
	if !strings.HasSuffix(signed, "signature="+want+"\n") {
		t.Fatalf("C++ のテストベクターと署名が違う: %q", signed)
	}
}

func signatureOf(t *testing.T, signed string) []byte {
	t.Helper()
	line := signed[strings.LastIndex(strings.TrimSuffix(signed, "\n"), "\n")+1:]
	sig, err := hex.DecodeString(strings.TrimSuffix(strings.TrimPrefix(line, "signature="), "\n"))
	if err != nil {
		t.Fatal(err)
	}
	return sig
}
