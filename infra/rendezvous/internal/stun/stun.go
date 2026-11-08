// SPDX-License-Identifier: AGPL-3.0-only

// Package stun は STUN Binding（RFC 8489）のサーバー側を提供する。
//
// 親機と子機はこのサーバーに Binding リクエストを送り、NAT の外から見た自分のアドレス
// （サーバー再帰アドレス）を知る。認証は行わない。返すのは送信元アドレスだけで、
// 秘密情報を含まないため、仕様どおり誰からのリクエストにも応答してよい。
//
// UDP の同じポートでランデブーメッセージも受けるので、IsMessage で STUN かどうかを判別する。
package stun

import (
	"encoding/binary"
	"hash/crc32"
	"net/netip"
)

const (
	// headerLen は STUN ヘッダーの長さ（型 2、長さ 2、Magic Cookie 4、トランザクション ID 12）。
	headerLen = 20

	// MagicCookie は RFC 8489 の固定値。ヘッダーの 4 バイト目から入り、STUN と他のプロトコルの判別に使う。
	MagicCookie uint32 = 0x2112A442

	// fingerprintXOR は FINGERPRINT 属性の CRC32 に XOR する値（ASCII の "STUN"）。
	fingerprintXOR uint32 = 0x5354554E

	// software は応答の SOFTWARE 属性に入れる名前。どのサーバーが応答したかを調査しやすくするため。
	software = "VOCROZ rendezvous"
)

// メッセージ型（クラスとメソッドを合わせた値）。
const (
	typeBindingRequest uint16 = 0x0001
	typeBindingSuccess uint16 = 0x0101
	typeBindingError   uint16 = 0x0111
)

// 属性の型。
const (
	attrUsername             uint16 = 0x0006
	attrMessageIntegrity     uint16 = 0x0008
	attrErrorCode            uint16 = 0x0009
	attrUnknownAttributes    uint16 = 0x000A
	attrRealm                uint16 = 0x0014
	attrNonce                uint16 = 0x0015
	attrMessageIntegrity256  uint16 = 0x001C
	attrUserhash             uint16 = 0x001E
	attrXorMappedAddress     uint16 = 0x0020
	attrSoftware             uint16 = 0x8022
	attrFingerprint          uint16 = 0x8028
	comprehensionOptionalMin uint16 = 0x8000
)

// codeUnknownAttribute は理解できない必須属性を含むリクエストへのエラーコード。
const codeUnknownAttribute = 420

// IsMessage は b が STUN メッセージの形をしているかを返す。
// 先頭 2 bit が 0 で、Magic Cookie が所定の位置にあるものを STUN とみなす。
func IsMessage(b []byte) bool {
	return len(b) >= headerLen && b[0]&0xC0 == 0 && binary.BigEndian.Uint32(b[4:8]) == MagicCookie
}

// Respond は Binding リクエスト req に対する応答を返す。from はリクエストの送信元アドレス。
// 応答すべきでないもの（形式の誤り、Binding リクエスト以外、FINGERPRINT の不一致）には ok = false を返す。
// 不正なパケットに応答しないのは、送信元を偽装した反射攻撃に使われないようにするため。
func Respond(req []byte, from netip.AddrPort) (resp []byte, ok bool) {
	if !IsMessage(req) || binary.BigEndian.Uint16(req[0:2]) != typeBindingRequest {
		return nil, false
	}
	length := int(binary.BigEndian.Uint16(req[2:4]))
	if length%4 != 0 || len(req) != headerLen+length {
		return nil, false
	}
	unknown, ok := scanAttributes(req)
	if !ok {
		return nil, false
	}
	var txID [12]byte
	copy(txID[:], req[8:20])

	if len(unknown) > 0 {
		return buildUnknownAttributeError(txID, unknown), true
	}
	return buildSuccess(txID, from), true
}

// scanAttributes は属性を走査し、理解できない必須属性の型を返す。
// 属性の長さが不正か、FINGERPRINT が最後にない、または値が一致しなければ ok = false を返す。
func scanAttributes(msg []byte) (unknown []uint16, ok bool) {
	for off := headerLen; off < len(msg); {
		if off+4 > len(msg) {
			return nil, false
		}
		typ := binary.BigEndian.Uint16(msg[off : off+2])
		vlen := int(binary.BigEndian.Uint16(msg[off+2 : off+4]))
		next := off + 4 + padded(vlen)
		if next > len(msg) {
			return nil, false
		}
		switch {
		case typ == attrFingerprint:
			if vlen != 4 || next != len(msg) {
				return nil, false
			}
			if binary.BigEndian.Uint32(msg[off+4:off+8]) != fingerprint(msg[:off]) {
				return nil, false
			}
		case typ >= comprehensionOptionalMin:
			// 理解できない任意属性は無視してよい（RFC 8489）。
		case isIgnoredAuthAttribute(typ):
			// このサーバーは認証しないので、認証の属性は理解したうえで無視する。
		default:
			unknown = append(unknown, typ)
		}
		off = next
	}
	return unknown, true
}

// isIgnoredAuthAttribute は認証に関わる必須属性かを返す。
// 長期・短期の資格情報を使うクライアントが送ってきても、420 を返さずに応答できるようにするため。
func isIgnoredAuthAttribute(typ uint16) bool {
	switch typ {
	case attrUsername, attrMessageIntegrity, attrRealm, attrNonce, attrMessageIntegrity256, attrUserhash:
		return true
	}
	return false
}

// buildSuccess は XOR-MAPPED-ADDRESS に from を入れた Binding 成功応答を作る。
func buildSuccess(txID [12]byte, from netip.AddrPort) []byte {
	addr := from.Addr().Unmap()
	value := make([]byte, 4, 20)
	value[1] = 0x01
	if addr.Is6() {
		value[1] = 0x02
	}
	binary.BigEndian.PutUint16(value[2:4], from.Port()^uint16(MagicCookie>>16))

	// アドレスは Magic Cookie（IPv6 では Magic Cookie とトランザクション ID の連結）と XOR する。
	// NAT 機器がペイロード内のアドレスを書き換えてしまうのを防ぐための RFC の規定。
	var mask [16]byte
	binary.BigEndian.PutUint32(mask[0:4], MagicCookie)
	copy(mask[4:], txID[:])
	raw := addr.AsSlice()
	for i := range raw {
		raw[i] ^= mask[i]
	}
	value = append(value, raw...)

	msg := newHeader(typeBindingSuccess, txID)
	msg = appendAttribute(msg, attrXorMappedAddress, value)
	msg = appendAttribute(msg, attrSoftware, []byte(software))
	return appendFingerprint(msg)
}

// buildUnknownAttributeError は 420 エラー応答を作り、UNKNOWN-ATTRIBUTES に理解できない属性を列挙する。
func buildUnknownAttributeError(txID [12]byte, unknown []uint16) []byte {
	reason := "Unknown Attribute"
	code := make([]byte, 4, 4+len(reason))
	code[2] = codeUnknownAttribute / 100
	code[3] = codeUnknownAttribute % 100
	code = append(code, reason...)

	list := make([]byte, 0, 2*len(unknown))
	for _, typ := range unknown {
		list = binary.BigEndian.AppendUint16(list, typ)
	}

	msg := newHeader(typeBindingError, txID)
	msg = appendAttribute(msg, attrErrorCode, code)
	msg = appendAttribute(msg, attrUnknownAttributes, list)
	msg = appendAttribute(msg, attrSoftware, []byte(software))
	return appendFingerprint(msg)
}

// newHeader は長さ 0 の STUN ヘッダーを作る。長さは appendAttribute が更新する。
func newHeader(typ uint16, txID [12]byte) []byte {
	msg := make([]byte, headerLen, 96)
	binary.BigEndian.PutUint16(msg[0:2], typ)
	binary.BigEndian.PutUint32(msg[4:8], MagicCookie)
	copy(msg[8:20], txID[:])
	return msg
}

// appendAttribute は属性を 4 バイト境界まで詰め物をして追加し、ヘッダーの長さを更新する。
func appendAttribute(msg []byte, typ uint16, value []byte) []byte {
	msg = binary.BigEndian.AppendUint16(msg, typ)
	msg = binary.BigEndian.AppendUint16(msg, uint16(len(value)))
	msg = append(msg, value...)
	for range padded(len(value)) - len(value) {
		msg = append(msg, 0)
	}
	binary.BigEndian.PutUint16(msg[2:4], uint16(len(msg)-headerLen))
	return msg
}

// appendFingerprint は FINGERPRINT 属性を追加する。
// CRC はこの属性を含めた長さをヘッダーに入れた状態で、属性の直前までに対して計算する（RFC 8489）。
func appendFingerprint(msg []byte) []byte {
	binary.BigEndian.PutUint16(msg[2:4], uint16(len(msg)+8-headerLen))
	value := binary.BigEndian.AppendUint32(nil, fingerprint(msg))
	return appendAttribute(msg, attrFingerprint, value)
}

// fingerprint は FINGERPRINT 属性の値を計算する。
func fingerprint(b []byte) uint32 {
	return crc32.ChecksumIEEE(b) ^ fingerprintXOR
}

// padded は n を 4 の倍数に切り上げる。
func padded(n int) int {
	return (n + 3) &^ 3
}
