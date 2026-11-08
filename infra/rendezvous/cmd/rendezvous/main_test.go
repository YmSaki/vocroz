// SPDX-License-Identifier: AGPL-3.0-only

package main

import "testing"

func TestParseOptionsDefaults(t *testing.T) {
	o, err := parseOptions(nil)
	if err != nil {
		t.Fatal(err)
	}
	if o.httpAddr != ":8080" || o.udpAddr != ":3478" || o.downloadURL != defaultDownloadURL || o.tlsCert != "" {
		t.Fatalf("parseOptions(nil) = %+v", o)
	}
}

func TestParseOptionsRejectsInconsistentArguments(t *testing.T) {
	for _, args := range [][]string{
		{"-tls-cert", "cert.pem"},
		{"-tls-key", "key.pem"},
		{"-download-url", "javascript:alert(1)"},
		{"-download-url", "/relative"},
		{"extra"},
		{"-unknown"},
	} {
		if _, err := parseOptions(args); err == nil {
			t.Errorf("parseOptions(%q) error = nil", args)
		}
	}
}
