// SPDX-License-Identifier: AGPL-3.0-only
//
// release: 更新マニフェストの鍵の生成と署名を行う、リリース担当者向けのツール。
//
//	go run . keygen -out <秘密鍵のファイル>
//	    Ed25519 の鍵を作り、秘密鍵（シード）を 16 進数でファイルに書き、公開鍵を表示する。
//	    公開鍵は CMake の VOCROZ_UPDATE_PUBLIC_KEY に渡す。秘密鍵はリポジトリに置かない。
//	go run . sign -key <秘密鍵のファイル> -version <版> -url <URL> -package <ZIP> [-publisher <発行者>]
//	    パッケージの SHA-256 とサイズを計算し、署名したマニフェストを標準出力に書く。
package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
)

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	var err error
	switch os.Args[1] {
	case "keygen":
		err = runKeygen(os.Args[2:])
	case "sign":
		err = runSign(os.Args[2:])
	default:
		usage()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage: release keygen -out <file> | release sign -key <file> -version <v> -url <url> -package <zip> [-publisher <name>]")
	os.Exit(2)
}

func runKeygen(args []string) error {
	fs := flag.NewFlagSet("keygen", flag.ExitOnError)
	out := fs.String("out", "", "秘密鍵（シード）を書くファイル")
	_ = fs.Parse(args)
	if *out == "" {
		return fmt.Errorf("-out を指定してください")
	}
	public, private, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return fmt.Errorf("鍵を生成できません: %w", err)
	}
	// 既存の鍵を誤って上書きしないよう、O_EXCL で新規作成だけを許す。
	f, err := os.OpenFile(*out, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		return fmt.Errorf("秘密鍵のファイルを作れません: %w", err)
	}
	defer f.Close()
	if _, err := fmt.Fprintln(f, hex.EncodeToString(private.Seed())); err != nil {
		return fmt.Errorf("秘密鍵を書けません: %w", err)
	}
	fmt.Println(hex.EncodeToString(public))
	return nil
}

func runSign(args []string) error {
	fs := flag.NewFlagSet("sign", flag.ExitOnError)
	keyPath := fs.String("key", "", "秘密鍵（シード）のファイル")
	version := fs.String("version", "", "更新後のバージョン")
	url := fs.String("url", "", "パッケージの HTTPS の URL")
	packagePath := fs.String("package", "", "パッケージ（ZIP）のファイル")
	publisher := fs.String("publisher", "", "Authenticode の発行者。空なら検証しない")
	_ = fs.Parse(args)
	if *keyPath == "" || *version == "" || *url == "" || *packagePath == "" {
		return fmt.Errorf("-key、-version、-url、-package を指定してください")
	}
	key, err := loadKey(*keyPath)
	if err != nil {
		return err
	}
	digest, size, err := hashFile(*packagePath)
	if err != nil {
		return err
	}
	body, err := Manifest{Version: *version, URL: *url, SHA256: digest, Size: size, Publisher: *publisher}.Body()
	if err != nil {
		return err
	}
	fmt.Print(Sign(body, key))
	return nil
}

func loadKey(path string) (ed25519.PrivateKey, error) {
	text, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("秘密鍵を読めません: %w", err)
	}
	seed, err := hex.DecodeString(strings.TrimSpace(string(text)))
	if err != nil || len(seed) != ed25519.SeedSize {
		return nil, fmt.Errorf("秘密鍵の形式が正しくありません")
	}
	return ed25519.NewKeyFromSeed(seed), nil
}

func hashFile(path string) (string, uint64, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", 0, fmt.Errorf("パッケージを開けません: %w", err)
	}
	defer f.Close()
	h := sha256.New()
	n, err := io.Copy(h, f)
	if err != nil {
		return "", 0, fmt.Errorf("パッケージを読めません: %w", err)
	}
	return hex.EncodeToString(h.Sum(nil)), uint64(n), nil
}
