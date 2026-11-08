// SPDX-License-Identifier: AGPL-3.0-only

// ランデブーメッセージのワイヤーフォーマット。親機と子機のクライアント実装
// （modules/network と modules/session）はこの定義に合わせる（AGENTS.md 4.4）。
//
// すべての整数はビッグエンディアン。共通ヘッダーは 13 バイト。
//
//	0  magic   "VRZ"（3 バイト）。先頭 2 bit が 01 なので STUN（先頭 2 bit が 00）と衝突しない
//	3  version 1
//	4  type    1: REGISTER、2: REGISTERED、3: PUNCH
//	5  txid    8 バイト。REGISTER の送信者が決め、対応する REGISTERED と PUNCH にそのまま入る
//
// 候補アドレス（candidate）は family（1 バイト。4 か 6）、port（2 バイト）、address（4 か 16 バイト）。
//
//	REGISTER   rendezvous_id(16) peer_id(16) role(1) count(1) candidate × count
//	REGISTERED status(1) flags(1) reflexive candidate
//	PUNCH      rendezvous_id(16) peer_id(16) peer_role(1) count(1) candidate × count
//
// REGISTERED の status は 0: 成功、1: サーバーの上限に達したため登録できなかった。
// flags の bit 0 は、相手の役割の登録がすでにあること（peer_present）を表す。
// PUNCH の peer_id と peer_role は相手のもので、候補アドレスは相手のサーバー再帰アドレスが先頭に来る。
// 登録者が受け取る PUNCH の txid は自分の REGISTER の txid、相手が受け取る PUNCH の txid は 0。

package udpapi

import (
	"encoding/binary"
	"errors"
	"net/netip"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
)

const (
	// protocolVersion はワイヤーフォーマットのバージョン。互換性のない変更で上げる。
	protocolVersion = 1

	headerLen = 13
)

// magic はランデブーメッセージの先頭 3 バイト。
var magic = [3]byte{'V', 'R', 'Z'}

// MessageType はランデブーメッセージの種類。
type MessageType uint8

const (
	TypeRegister   MessageType = 1
	TypeRegistered MessageType = 2
	TypePunch      MessageType = 3
)

// RegisterStatus は REGISTERED の結果。
type RegisterStatus uint8

const (
	StatusOK       RegisterStatus = 0
	StatusCapacity RegisterStatus = 1
)

const (
	familyIPv4 = 4
	familyIPv6 = 6

	flagPeerPresent = 0x01
)

// errMalformed はメッセージの形式が正しくないことを表す。
var errMalformed = errors.New("malformed rendezvous message")

// TxID はトランザクション ID。
type TxID [8]byte

// Register は REGISTER メッセージ。親機と子機が送る。
type Register struct {
	TxID         TxID
	RendezvousID signaling.ID
	PeerID       signaling.ID
	Role         signaling.Role
	Candidates   []netip.AddrPort
}

// Registered は REGISTERED メッセージ。サーバーが REGISTER の送信者に返す。
type Registered struct {
	TxID        TxID
	Status      RegisterStatus
	PeerPresent bool
	// Reflexive はサーバーから見た送信者のアドレス。
	Reflexive netip.AddrPort
}

// Punch は PUNCH メッセージ。サーバーが親機と子機の双方に同時に送り、ホールパンチの開始を知らせる。
type Punch struct {
	TxID         TxID
	RendezvousID signaling.ID
	PeerID       signaling.ID
	PeerRole     signaling.Role
	Candidates   []netip.AddrPort
}

// IsMessage は b がランデブーメッセージの形をしているかを返す。
func IsMessage(b []byte) bool {
	return len(b) >= headerLen && [3]byte(b[0:3]) == magic
}

// Append は m をエンコードして b に追加する。
func (m Register) Append(b []byte) []byte {
	b = appendHeader(b, TypeRegister, m.TxID)
	return appendPeerBlock(b, m.RendezvousID, m.PeerID, m.Role, m.Candidates)
}

// Append は m をエンコードして b に追加する。
func (m Registered) Append(b []byte) []byte {
	b = appendHeader(b, TypeRegistered, m.TxID)
	var flags byte
	if m.PeerPresent {
		flags |= flagPeerPresent
	}
	b = append(b, byte(m.Status), flags)
	return appendCandidate(b, m.Reflexive)
}

// Append は m をエンコードして b に追加する。
func (m Punch) Append(b []byte) []byte {
	b = appendHeader(b, TypePunch, m.TxID)
	return appendPeerBlock(b, m.RendezvousID, m.PeerID, m.PeerRole, m.Candidates)
}

// ParseRegister は REGISTER をデコードして検証する。
// 形式、バージョン、役割、候補アドレスの数と値のいずれかが不正ならエラーを返す。
func ParseRegister(b []byte) (Register, error) {
	r, err := newReader(b, TypeRegister)
	if err != nil {
		return Register{}, err
	}
	var m Register
	m.TxID = r.txID
	m.RendezvousID, m.PeerID, m.Role, m.Candidates = r.peerBlock(signaling.MaxLocalCandidates)
	return m, r.finish()
}

// ParseRegistered は REGISTERED をデコードして検証する。
func ParseRegistered(b []byte) (Registered, error) {
	r, err := newReader(b, TypeRegistered)
	if err != nil {
		return Registered{}, err
	}
	m := Registered{TxID: r.txID}
	status, flags := r.byte(), r.byte()
	switch RegisterStatus(status) {
	case StatusOK, StatusCapacity:
		m.Status = RegisterStatus(status)
	default:
		r.fail()
	}
	m.PeerPresent = flags&flagPeerPresent != 0
	m.Reflexive = r.candidate()
	return m, r.finish()
}

// ParsePunch は PUNCH をデコードして検証する。
func ParsePunch(b []byte) (Punch, error) {
	r, err := newReader(b, TypePunch)
	if err != nil {
		return Punch{}, err
	}
	m := Punch{TxID: r.txID}
	// サーバーは申告されたローカルアドレスに再帰アドレスを 1 つ加えて送る。
	m.RendezvousID, m.PeerID, m.PeerRole, m.Candidates = r.peerBlock(signaling.MaxLocalCandidates + 1)
	return m, r.finish()
}

func appendHeader(b []byte, typ MessageType, txID TxID) []byte {
	b = append(b, magic[:]...)
	b = append(b, protocolVersion, byte(typ))
	return append(b, txID[:]...)
}

// appendPeerBlock は REGISTER と PUNCH に共通する本体（ID、役割、候補アドレスの列）を追加する。
func appendPeerBlock(b []byte, rendezvousID, peerID signaling.ID, role signaling.Role, cands []netip.AddrPort) []byte {
	b = append(b, rendezvousID[:]...)
	b = append(b, peerID[:]...)
	b = append(b, byte(role), byte(len(cands)))
	for _, c := range cands {
		b = appendCandidate(b, c)
	}
	return b
}

func appendCandidate(b []byte, c netip.AddrPort) []byte {
	addr := c.Addr().Unmap()
	if addr.Is4() {
		b = append(b, familyIPv4)
	} else {
		b = append(b, familyIPv6)
	}
	b = binary.BigEndian.AppendUint16(b, c.Port())
	return append(b, addr.AsSlice()...)
}

// reader はメッセージを先頭から読む。途中で不正を見つけたら以降の読み取りを空振りさせ、finish でエラーを返す。
// 読み取りのたびにエラーを確かめる分岐を書かずに済むようにするため。
type reader struct {
	b    []byte
	off  int
	bad  bool
	txID TxID
}

func newReader(b []byte, want MessageType) (*reader, error) {
	if !IsMessage(b) || b[3] != protocolVersion || MessageType(b[4]) != want {
		return nil, errMalformed
	}
	r := &reader{b: b, off: headerLen}
	copy(r.txID[:], b[5:headerLen])
	return r, nil
}

func (r *reader) fail() { r.bad = true }

func (r *reader) take(n int) []byte {
	if r.bad || r.off+n > len(r.b) {
		r.bad = true
		return make([]byte, n)
	}
	s := r.b[r.off : r.off+n]
	r.off += n
	return s
}

func (r *reader) byte() byte { return r.take(1)[0] }

func (r *reader) id() signaling.ID { return signaling.ID(r.take(signaling.IDLen)) }

func (r *reader) role() signaling.Role {
	role := signaling.Role(r.byte())
	switch role {
	case signaling.RoleHost, signaling.RoleClient:
		return role
	}
	r.fail()
	return 0
}

// candidate は候補アドレスを 1 つ読む。未指定アドレス、マルチキャスト、ポート 0 は不正とする。
// 相手がそのアドレスへ送信しても届かず、サーバーを踏み台にした誘導にも使えないようにするため。
func (r *reader) candidate() netip.AddrPort {
	family := r.byte()
	port := binary.BigEndian.Uint16(r.take(2))
	var addr netip.Addr
	switch family {
	case familyIPv4:
		addr = netip.AddrFrom4([4]byte(r.take(4)))
	case familyIPv6:
		addr = netip.AddrFrom16([16]byte(r.take(16))).Unmap()
	default:
		r.fail()
		return netip.AddrPort{}
	}
	if addr.IsUnspecified() || addr.IsMulticast() || port == 0 {
		r.fail()
	}
	return netip.AddrPortFrom(addr, port)
}

// peerBlock は REGISTER と PUNCH に共通する本体を読む。候補アドレスが maxCands 個を超えれば不正とする。
func (r *reader) peerBlock(maxCands int) (rendezvousID, peerID signaling.ID, role signaling.Role, cands []netip.AddrPort) {
	rendezvousID, peerID, role = r.id(), r.id(), r.role()
	n := int(r.byte())
	if n > maxCands {
		r.fail()
		return
	}
	cands = make([]netip.AddrPort, 0, n)
	for range n {
		cands = append(cands, r.candidate())
	}
	return
}

// finish はすべてのバイトを過不足なく読めたかを確かめる。
func (r *reader) finish() error {
	if r.bad || r.off != len(r.b) {
		return errMalformed
	}
	return nil
}
