// SPDX-License-Identifier: AGPL-3.0-only

// Package punchprobe は、ランデブーサーバー越しの UDP ホールパンチが成立するかを確かめる試験用のピア。
// 親機と子機のクライアント実装（modules/network と modules/session）ができるまでの代わりに、
// 同じランデブーメッセージ（udpapi のワイヤーフォーマット）で REGISTER を送り、PUNCH を受けたら
// 相手の候補アドレスへ同時に送信して、双方向に届くかを判定する。
// ネットワーク名前空間で NAT の種類を模した試験（tests/nat/）から使い、NAT の組み合わせごとに
// 成否と、不成立のときの原因の区別を確かめる。製品には含めない。
package punchprobe

import (
	"bytes"
	"context"
	"crypto/rand"
	"errors"
	"net"
	"net/netip"
	"time"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
	"github.com/YmSaki/vocroz/infra/rendezvous/internal/udpapi"
)

// Outcome はホールパンチの試行の結果。
type Outcome string

const (
	// OutcomeConnected は、相手との間で双方向にパケットが届いたこと。
	OutcomeConnected Outcome = "CONNECTED"
	// OutcomeUnreachable は、サーバーが相手の候補アドレスを渡したのに、相手と双方向に通信できなかったこと
	// （NAT 越え不成立）。製品の FAILED の原因コード UNREACHABLE に当たる。
	OutcomeUnreachable Outcome = "UNREACHABLE"
	// OutcomeRendezvousUnreachable は、ランデブーサーバーから REGISTERED が返らなかったこと
	// （サーバーの停止、UDP の遮断）。
	OutcomeRendezvousUnreachable Outcome = "RENDEZVOUS_UNREACHABLE"
	// OutcomePeerNotFound は、サーバーには登録できたが、相手が同じランデブー ID で現れなかったこと。
	OutcomePeerNotFound Outcome = "PEER_NOT_FOUND"
)

const (
	// registerInterval は REGISTER の再送の間隔。製品の子機は接続できるまで再送する（ADR-0012）。
	// 試験の時間を短くするため、製品より短くする。
	registerInterval = 250 * time.Millisecond
	// probeInterval は相手への試験パケットの送信間隔。NAT のマッピングが開くまで送り続ける。
	probeInterval = 50 * time.Millisecond
	// linger は接続できたあとも応答を続ける時間。こちらが先に成功して終了すると、
	// 相手がこちらの応答を受け取れずに失敗と判定してしまうため。
	linger = time.Second
	// maxDatagram は受信バッファの大きさ。
	maxDatagram = 1500
)

// probeMagic は相手との試験パケットの先頭。ランデブーメッセージ（VRZ）と STUN と区別できる値にする。
var probeMagic = []byte("PRB")

const (
	probePing byte = 1
	probePong byte = 2
)

// Config は試行の設定。
type Config struct {
	// Server はランデブーサーバーの UDP のアドレス。
	Server netip.AddrPort
	// RendezvousID は親機と子機で共有するランデブー ID。
	RendezvousID signaling.ID
	// Role は自分の役割。
	Role signaling.Role
	// Local は REGISTER に載せる自分のローカル候補アドレス。NAT の内側のアドレスを渡す。
	Local []netip.AddrPort
}

// Result は試行の結果と、その判断の材料。
type Result struct {
	Outcome Outcome
	// Reflexive はサーバーが見た自分のアドレス（REGISTERED を受けたとき）。
	Reflexive netip.AddrPort
	// PeerCandidates はサーバーから受け取った相手の候補アドレス（PUNCH を受けたとき）。
	PeerCandidates []netip.AddrPort
	// Via は相手からの応答が届いた送信元（CONNECTED のとき）。
	Via netip.AddrPort
}

// Run は ctx の期限まで、ホールパンチを試みる。conn は試行の間ずっと同じソケットを使う
// （サーバーに見えたマッピングを、そのまま相手との通信に使うため）。
// ctx の期限までに双方向に通信できなければ、それまでに分かったことから原因を判定して返す。
func Run(ctx context.Context, conn *net.UDPConn, cfg Config) (Result, error) {
	var self signaling.ID
	if _, err := rand.Read(self[:]); err != nil {
		return Result{}, err
	}
	p := &probe{conn: conn, cfg: cfg, self: self, targets: map[netip.AddrPort]bool{}}
	return p.run(ctx)
}

type probe struct {
	conn    *net.UDPConn
	cfg     Config
	self    signaling.ID
	peer    signaling.ID
	result  Result
	hasPeer bool
	// targets は試験パケットの宛先。サーバーから受け取った候補と、相手から届いた送信元（ピア再帰アドレス）。
	targets      map[netip.AddrPort]bool
	registered   bool
	connectedAt  time.Time
	nextRegister time.Time
	nextProbe    time.Time
}

func (p *probe) run(ctx context.Context) (Result, error) {
	buf := make([]byte, maxDatagram)
	for {
		now := time.Now()
		if !p.connectedAt.IsZero() && now.Sub(p.connectedAt) >= linger {
			return p.result, nil
		}
		if err := ctx.Err(); err != nil {
			return p.verdict(), nil
		}
		if p.connectedAt.IsZero() && !now.Before(p.nextRegister) {
			if err := p.sendRegister(); err != nil {
				return Result{}, err
			}
			p.nextRegister = now.Add(registerInterval)
		}
		if !now.Before(p.nextProbe) {
			p.sendPings()
			p.nextProbe = now.Add(probeInterval)
		}
		_ = p.conn.SetReadDeadline(earliest(p.nextRegister, p.nextProbe, now.Add(probeInterval)))
		n, from, err := p.conn.ReadFromUDPAddrPort(buf)
		if err != nil {
			var timeout net.Error
			if errors.As(err, &timeout) && timeout.Timeout() {
				continue
			}
			return Result{}, err
		}
		p.handle(buf[:n], netip.AddrPortFrom(from.Addr().Unmap(), from.Port()))
	}
}

// verdict は期限までに接続できなかったときの原因を、試行の進み具合から判定する。
func (p *probe) verdict() Result {
	r := p.result
	switch {
	case !p.connectedAt.IsZero():
		r.Outcome = OutcomeConnected
	case !p.registered:
		r.Outcome = OutcomeRendezvousUnreachable
	case !p.hasPeer:
		r.Outcome = OutcomePeerNotFound
	default:
		r.Outcome = OutcomeUnreachable
	}
	return r
}

func (p *probe) sendRegister() error {
	var tx udpapi.TxID
	if _, err := rand.Read(tx[:]); err != nil {
		return err
	}
	msg := udpapi.Register{
		TxID:         tx,
		RendezvousID: p.cfg.RendezvousID,
		PeerID:       p.self,
		Role:         p.cfg.Role,
		Candidates:   p.cfg.Local,
	}
	// 送信の失敗（経路がないなど）はサーバーに届かなかったのと同じに扱い、再送で回復させる。
	_, _ = p.conn.WriteToUDPAddrPort(msg.Append(nil), p.cfg.Server)
	return nil
}

func (p *probe) sendPings() {
	if !p.hasPeer {
		return
	}
	ping := p.message(probePing)
	for target := range p.targets {
		_, _ = p.conn.WriteToUDPAddrPort(ping, target)
	}
}

func (p *probe) message(kind byte) []byte {
	b := append([]byte(nil), probeMagic...)
	b = append(b, kind)
	return append(b, p.self[:]...)
}

func (p *probe) handle(pkt []byte, from netip.AddrPort) {
	if from == p.cfg.Server && udpapi.IsMessage(pkt) {
		p.handleRendezvous(pkt)
		return
	}
	if len(pkt) != len(probeMagic)+1+signaling.IDLen || !bytes.Equal(pkt[:len(probeMagic)], probeMagic) {
		return
	}
	var sender signaling.ID
	copy(sender[:], pkt[len(probeMagic)+1:])
	// PUNCH より先に相手の試験パケットが届くことがある（相手の方が先に PUNCH を受けた）。
	// 送信元が分かっているので、その時点で相手として扱って応答する。
	if !p.hasPeer {
		p.peer, p.hasPeer = sender, true
	}
	if sender != p.peer {
		return
	}
	switch pkt[len(probeMagic)] {
	case probePing:
		// 相手から届いた送信元はピア再帰アドレス。対称型 NAT ではサーバーが見たアドレスと違うので、
		// ここへ送り返すことで、片側だけが対称型の組み合わせでも通信を成立させる。
		p.targets[from] = true
		_, _ = p.conn.WriteToUDPAddrPort(p.message(probePong), from)
	case probePong:
		if p.connectedAt.IsZero() {
			p.connectedAt = time.Now()
			p.result.Outcome = OutcomeConnected
			p.result.Via = from
		}
	}
}

func (p *probe) handleRendezvous(pkt []byte) {
	if reg, err := udpapi.ParseRegistered(pkt); err == nil {
		if reg.Status == udpapi.StatusOK {
			p.registered = true
			p.result.Reflexive = reg.Reflexive
		}
		return
	}
	punch, err := udpapi.ParsePunch(pkt)
	if err != nil || punch.RendezvousID != p.cfg.RendezvousID {
		return
	}
	// PUNCH が届いたなら登録も済んでいる（REGISTERED が失われた場合）。
	p.registered = true
	p.peer, p.hasPeer = punch.PeerID, true
	p.result.PeerCandidates = punch.Candidates
	for _, c := range punch.Candidates {
		p.targets[c] = true
	}
	p.sendPings()
}

func earliest(times ...time.Time) time.Time {
	min := times[0]
	for _, t := range times[1:] {
		if t.Before(min) {
			min = t
		}
	}
	return min
}
