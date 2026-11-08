// SPDX-License-Identifier: AGPL-3.0-only

// Package udpapi はランデブーサーバーの UDP 側の外部インターフェース。
//
// 1 つの UDP ポートで STUN Binding と独自のランデブーメッセージ（候補アドレスの交換と
// ホールパンチ開始の同期）を受け、パケットの先頭で振り分ける（STUN は Magic Cookie、
// ランデブーメッセージは "VRZ"）。どちらでもないパケットと不正なパケットには応答しない。
// 音声は扱わず、確立後の P2P 通信はこのサーバーを通らない。
package udpapi

import (
	"errors"
	"fmt"
	"net"
	"net/netip"

	"github.com/YmSaki/vocroz/infra/rendezvous/internal/signaling"
	"github.com/YmSaki/vocroz/infra/rendezvous/internal/stun"
)

// maxDatagram は受信バッファの大きさ。正しいメッセージはどれも 256 バイトに収まるが、
// 大きすぎるパケットを切り詰めて誤って受理しないように、一般的な MTU より大きくしておく。
const maxDatagram = 2048

// Datagram は送信するパケット。
type Datagram struct {
	To      netip.AddrPort
	Payload []byte
}

// Handler は受信したパケットから送信するパケットを決める。ソケットを持たないので単体テストできる。
type Handler struct {
	table *signaling.Table
}

// NewHandler は table を使う Handler を返す。
func NewHandler(table *signaling.Table) *Handler {
	return &Handler{table: table}
}

// Handle は from から届いた pkt を処理し、送信するパケットを返す。応答しない場合は空を返す。
// 複数の goroutine から同時に呼び出してよい。
func (h *Handler) Handle(pkt []byte, from netip.AddrPort) []Datagram {
	// デュアルスタックのソケットでは IPv4 の送信元が IPv4 射影アドレスで届くので、IPv4 にそろえる。
	from = netip.AddrPortFrom(from.Addr().Unmap(), from.Port())
	switch {
	case stun.IsMessage(pkt):
		if resp, ok := stun.Respond(pkt, from); ok {
			return []Datagram{{To: from, Payload: resp}}
		}
	case IsMessage(pkt):
		return h.handleRegister(pkt, from)
	}
	return nil
}

func (h *Handler) handleRegister(pkt []byte, from netip.AddrPort) []Datagram {
	req, err := ParseRegister(pkt)
	if err != nil {
		return nil
	}
	res, err := h.table.Register(signaling.Registration{
		RendezvousID: req.RendezvousID,
		PeerID:       req.PeerID,
		Role:         req.Role,
		Observed:     from,
		Local:        req.Candidates,
	})
	ack := Registered{TxID: req.TxID, Status: StatusOK, PeerPresent: res.PeerPresent, Reflexive: from}
	if errors.Is(err, signaling.ErrCapacity) {
		ack.Status = StatusCapacity
	}

	out := []Datagram{{To: from, Payload: ack.Append(nil)}}
	for _, intro := range res.Introductions {
		punch := Punch{
			RendezvousID: req.RendezvousID,
			PeerID:       intro.PeerID,
			PeerRole:     intro.PeerRole,
			Candidates:   intro.Candidates,
		}
		if intro.To == from {
			punch.TxID = req.TxID
		}
		out = append(out, Datagram{To: intro.To, Payload: punch.Append(nil)})
	}
	return out
}

// Serve は conn からパケットを受け、Handler の結果を送信する。conn が閉じられると nil を返す。
// 処理は短時間で終わるので、1 つの goroutine で順に処理する。
func Serve(conn *net.UDPConn, h *Handler) error {
	buf := make([]byte, maxDatagram)
	for {
		n, from, err := conn.ReadFromUDPAddrPort(buf)
		if err != nil {
			if errors.Is(err, net.ErrClosed) {
				return nil
			}
			return fmt.Errorf("udp read: %w", err)
		}
		if n == len(buf) {
			// バッファいっぱいのパケットは切り詰められている可能性があるので捨てる。
			continue
		}
		for _, d := range h.Handle(buf[:n], from) {
			// 送信の失敗（相手のネットワークの到達不能など）は個々のクライアントの問題なので、サーバーは止めない。
			_, _ = conn.WriteToUDPAddrPort(d.Payload, d.To)
		}
	}
}
