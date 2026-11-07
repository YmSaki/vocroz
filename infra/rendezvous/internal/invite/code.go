// SPDX-License-Identifier: AGPL-3.0-only

// 招待コードの形式（生成、正規化、表示）を定義する。
// 招待コードは人が読み上げたり手で入力したりするため、紛らわしい文字を含まない
// Crockford Base32 を使い、入力時の表記ゆれ（小文字、ハイフン、O と 0 など）を吸収する。

package invite

import (
	"errors"
	"fmt"
	"io"
	"strings"
)

// codeAlphabet は Crockford Base32 の 32 文字。I、L、O、U を含まないので読み間違いが起きにくい。
const codeAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

// CodeLength は招待コードの文字数（区切りのハイフンを除く）。
// 1 文字 5 bit なので 50 bit。招待は 30 分で失効し、コードは参加の申請権しか与えない
// （親機の承認が必要）ため、総当たりで有効なコードを引き当てる確率は実用上無視できる。
const CodeLength = 10

// codeGroupSize は表示時にハイフンで区切る文字数（XXXXX-XXXXX）。
const codeGroupSize = 5

// ErrInvalidCode は招待コードの形式が正しくないことを表す。
var ErrInvalidCode = errors.New("invalid invite code")

// Code は正規化済みの招待コード（ハイフンを含まない大文字 10 文字）。
// ParseCode か newCode を通した値だけが入る。
type Code string

// String は人に見せる形（XXXXX-XXXXX）を返す。
func (c Code) String() string {
	s := string(c)
	if len(s) != CodeLength {
		return s
	}
	return s[:codeGroupSize] + "-" + s[codeGroupSize:]
}

// ParseCode は人が入力した招待コードを正規化して検証する。
// 大文字小文字、ハイフン、空白の違いを無視し、O を 0、I と L を 1 として読む（Crockford Base32 の規則）。
// 形式が正しくなければ ErrInvalidCode を返す。
func ParseCode(s string) (Code, error) {
	var b strings.Builder
	b.Grow(CodeLength)
	for _, r := range strings.ToUpper(s) {
		switch r {
		case '-', ' ':
			continue
		case 'O':
			r = '0'
		case 'I', 'L':
			r = '1'
		}
		if r > 0x7F || !strings.ContainsRune(codeAlphabet, r) {
			return "", ErrInvalidCode
		}
		b.WriteRune(r)
		if b.Len() > CodeLength {
			return "", ErrInvalidCode
		}
	}
	if b.Len() != CodeLength {
		return "", ErrInvalidCode
	}
	return Code(b.String()), nil
}

// newCode は乱数源 rand から招待コードを 1 つ生成する。
// 32 は 256 の約数なので、バイトの下位 5 bit を使っても偏りは生じない。
func newCode(rand io.Reader) (Code, error) {
	var buf [CodeLength]byte
	if _, err := io.ReadFull(rand, buf[:]); err != nil {
		return "", fmt.Errorf("generate invite code: %w", err)
	}
	out := make([]byte, CodeLength)
	for i, v := range buf {
		out[i] = codeAlphabet[v&0x1F]
	}
	return Code(out), nil
}
