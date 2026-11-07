// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新マニフェストの本文の組み立てと、Ed25519 の署名。
// 形式は docs/adr/0009-installer-and-updater.md と、
// modules/update/src/wire/manifest_codec.cpp のパーサーに合わせる。
package main

import (
	"crypto/ed25519"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"
)

// Manifest は署名する前の更新マニフェストの内容。
type Manifest struct {
	Version   string
	URL       string
	SHA256    string // 小文字の 16 進数
	Size      uint64
	Publisher string // 空なら Authenticode を検証しない
}

// manifestFormat はマニフェストの形式の版。C++ 側の kManifestFormat と同じ値にする。
const manifestFormat = "1"

// productID は C++ 側の kProductId と同じ値にする。
const productID = "vocroz"

// Body は署名対象の本文（キーの順序は固定、各行は LF で終わる）を返す。
func (m Manifest) Body() (string, error) {
	if !strings.HasPrefix(m.URL, "https://") {
		return "", errors.New("url は https:// で始める必要があります")
	}
	if len(m.SHA256) != 64 || strings.ToLower(m.SHA256) != m.SHA256 {
		return "", errors.New("sha256 は 64 桁の小文字の 16 進数で指定します")
	}
	if _, err := hex.DecodeString(m.SHA256); err != nil {
		return "", fmt.Errorf("sha256 が 16 進数ではありません: %w", err)
	}
	for _, v := range []string{m.Version, m.URL, m.Publisher} {
		if strings.ContainsAny(v, "\r\n") {
			return "", errors.New("値に改行を含めることはできません")
		}
	}
	var b strings.Builder
	fmt.Fprintf(&b, "format=%s\n", manifestFormat)
	fmt.Fprintf(&b, "product=%s\n", productID)
	fmt.Fprintf(&b, "version=%s\n", m.Version)
	fmt.Fprintf(&b, "url=%s\n", m.URL)
	fmt.Fprintf(&b, "sha256=%s\n", m.SHA256)
	fmt.Fprintf(&b, "size=%d\n", m.Size)
	fmt.Fprintf(&b, "publisher=%s\n", m.Publisher)
	return b.String(), nil
}

// Sign は本文に署名の行を加えたマニフェスト全体を返す。
func Sign(body string, key ed25519.PrivateKey) string {
	signature := ed25519.Sign(key, []byte(body))
	return body + "signature=" + hex.EncodeToString(signature) + "\n"
}
