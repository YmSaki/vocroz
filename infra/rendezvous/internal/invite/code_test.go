// SPDX-License-Identifier: AGPL-3.0-only

package invite

import (
	"bytes"
	"errors"
	"testing"
)

func TestParseCodeNormalizesInput(t *testing.T) {
	tests := []struct {
		in   string
		want Code
	}{
		{"ABCDE-FGHJK", "ABCDEFGHJK"},
		{"abcde fghjk", "ABCDEFGHJK"},
		{"oOiIl-23456", "0011123456"},
	}
	for _, tt := range tests {
		got, err := ParseCode(tt.in)
		if err != nil {
			t.Fatalf("ParseCode(%q) error = %v", tt.in, err)
		}
		if got != tt.want {
			t.Fatalf("ParseCode(%q) = %q, want %q", tt.in, got, tt.want)
		}
	}
}

func TestParseCodeRejectsMalformedInput(t *testing.T) {
	for _, in := range []string{"", "ABCDE", "ABCDE-FGHJKM", "ABCDE-FGHJU", "ABCDE-FGHJ!", "ＡBCDE-FGHJK"} {
		if _, err := ParseCode(in); !errors.Is(err, ErrInvalidCode) {
			t.Fatalf("ParseCode(%q) error = %v, want ErrInvalidCode", in, err)
		}
	}
}

func TestCodeStringInsertsHyphen(t *testing.T) {
	if got := Code("ABCDEFGHJK").String(); got != "ABCDE-FGHJK" {
		t.Fatalf("String() = %q", got)
	}
}

func TestNewCodeRoundTripsThroughParseCode(t *testing.T) {
	code, err := newCode(bytes.NewReader(bytes.Repeat([]byte{0xFF, 0x00, 0x7A}, 4)))
	if err != nil {
		t.Fatal(err)
	}
	parsed, err := ParseCode(code.String())
	if err != nil || parsed != code {
		t.Fatalf("ParseCode(%q) = %q, %v", code.String(), parsed, err)
	}
}
