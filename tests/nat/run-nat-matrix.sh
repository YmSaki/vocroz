#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Linux のネットワーク名前空間で NAT の種類（フルコーン、ポート制限、対称型）を模し、
# ランデブーサーバー越しの UDP ホールパンチの成否と、不成立のときの原因の区別を確かめる。
# 組み合わせごとに名前空間を作り直し、親機側と子機側で punchprobe を 1 つずつ動かして、
# 結果が期待どおりかを表にする。期待と違う組み合わせがあれば終了コード 1 で終わる。
#
# 構成（アドレスは文書用の範囲。実在のネットワークには出ない）:
#
#   [host-a 10.0.1.2] --lan-- [nat-a 203.0.113.2] --wan--+
#                                                        [inet 192.0.2.1: ランデブーサーバー]
#   [host-b 10.0.2.2] --lan-- [nat-b 203.0.113.6] --wan--+
#   [public 203.0.113.10] ------------------------------+   （直結の経路の確認用。NAT なし）
#
# NAT の種類の模し方（iptables）:
#   full-cone        MASQUERADE（ポートを保つ）と、WAN に届く UDP をすべて内側のホストへ DNAT。
#                    外からは、マッピングを開いた後は誰からでも届く（エンドポイント非依存のフィルタ）。
#   port-restricted  MASQUERADE と、WAN から来た新しい接続の破棄。送ったことのある相手のアドレスと
#                    ポートからしか届かない（Linux の既定の振る舞い）。
#   symmetric        MASQUERADE --random-fully（宛先ごとに別の外側ポート）と、新しい接続の破棄。
# 新しい接続は conntrack が確定する前の mangle で捨てる。nat の段階まで進めると、相手の先着の
# パケットが conntrack に残り、こちらの送信の外側ポートを変えてしまう（ホールパンチが成立しなくなる）。
#
# 使い方: sudo tests/nat/run-nat-matrix.sh [作業ディレクトリ]
#   必要なもの: root 権限、iproute2、iptables、Go（ランデブーサーバーと punchprobe をビルドする）

set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly WORK_DIR="${1:-$(mktemp -d)}"
readonly PREFIX="vz"
readonly SERVER_ADDR="192.0.2.1"
readonly SERVER_PORT="3478"
readonly RENDEZVOUS_ID="000102030405060708090a0b0c0d0e0f"
# 1 回の試行の上限。ホールパンチは数百 ms で決まるので、成功するはずの組み合わせが
# CI の揺らぎで落ちない程度に長くする。
readonly PROBE_TIMEOUT="4s"

log() { printf '%s\n' "$*" >&2; }

ns() { local name="$1"; shift; ip netns exec "${PREFIX}-${name}" "$@"; }

build_binaries() {
    log "building rendezvous and punchprobe into ${WORK_DIR}"
    (cd "${REPO_ROOT}/infra/rendezvous" &&
        go build -o "${WORK_DIR}/rendezvous" ./cmd/rendezvous &&
        go build -o "${WORK_DIR}/punchprobe" ./cmd/punchprobe)
}

teardown() {
    for name in host-a host-b nat-a nat-b public inet; do
        ip netns del "${PREFIX}-${name}" 2>/dev/null || true
    done
}

# veth で 2 つの名前空間をつなぎ、アドレスを振る。
link() {
    local left="$1" left_if="$2" left_addr="$3" right="$4" right_if="$5" right_addr="$6"
    ip link add "${PREFIX}t0" type veth peer name "${PREFIX}t1"
    ip link set "${PREFIX}t0" netns "${PREFIX}-${left}"
    ip link set "${PREFIX}t1" netns "${PREFIX}-${right}"
    ns "${left}" ip link set "${PREFIX}t0" name "${left_if}"
    ns "${right}" ip link set "${PREFIX}t1" name "${right_if}"
    ns "${left}" ip addr add "${left_addr}" dev "${left_if}"
    ns "${right}" ip addr add "${right_addr}" dev "${right_if}"
    ns "${left}" ip link set "${left_if}" up
    ns "${right}" ip link set "${right_if}" up
}

setup_topology() {
    teardown
    for name in host-a host-b nat-a nat-b public inet; do
        ip netns add "${PREFIX}-${name}"
        ns "${name}" ip link set lo up
    done
    for name in nat-a nat-b inet; do
        ns "${name}" sysctl -qw net.ipv4.ip_forward=1
    done
    ns inet ip addr add "${SERVER_ADDR}/32" dev lo
    link inet wan-a 203.0.113.1/30 nat-a wan 203.0.113.2/30
    link inet wan-b 203.0.113.5/30 nat-b wan 203.0.113.6/30
    link inet wan-p 203.0.113.9/30 public wan 203.0.113.10/30
    link nat-a lan 10.0.1.1/24 host-a lan 10.0.1.2/24
    link nat-b lan 10.0.2.1/24 host-b lan 10.0.2.2/24
    ns nat-a ip route add default via 203.0.113.1
    ns nat-b ip route add default via 203.0.113.5
    ns public ip route add default via 203.0.113.9
    ns host-a ip route add default via 10.0.1.1
    ns host-b ip route add default via 10.0.2.1
    # inet は 10.0.0.0/8 への経路を持たない。プライベートの候補アドレスはインターネットから届かない。
}

# NAT の種類を設定する。$1: nat-a か nat-b、$2: 種類、$3: 内側のホストのアドレス。
configure_nat() {
    local router="$1" kind="$2" inside="$3"
    case "${kind}" in
        full-cone)
            ns "${router}" iptables -t nat -A POSTROUTING -o wan -j MASQUERADE
            ns "${router}" iptables -t nat -A PREROUTING -i wan -p udp -j DNAT --to-destination "${inside}"
            ;;
        port-restricted)
            ns "${router}" iptables -t nat -A POSTROUTING -o wan -j MASQUERADE
            ns "${router}" iptables -t mangle -A PREROUTING -i wan -m conntrack --ctstate NEW -j DROP
            ;;
        symmetric)
            ns "${router}" iptables -t nat -A POSTROUTING -o wan -j MASQUERADE --random-fully
            ns "${router}" iptables -t mangle -A PREROUTING -i wan -m conntrack --ctstate NEW -j DROP
            ;;
        *)
            log "unknown NAT kind: ${kind}"
            return 1
            ;;
    esac
}

start_server() {
    ns inet "${WORK_DIR}/rendezvous" -udp "${SERVER_ADDR}:${SERVER_PORT}" -http "127.0.0.1:18080" \
        >"${WORK_DIR}/rendezvous.log" 2>&1 &
    SERVER_PID=$!
    # 待ち受けを始めるまで待つ（ss が UDP のソケットを見つけるまで）。
    for _ in $(seq 50); do
        if ns inet ss -Hlun "sport = :${SERVER_PORT}" | grep -q .; then
            return 0
        fi
        sleep 0.1
    done
    log "rendezvous server did not start"
    return 1
}

stop_server() {
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
        SERVER_PID=""
    fi
}

# punchprobe を動かし、結果（CONNECTED など）を標準出力に返す。
# $1: 名前空間、$2: 役割、$3: ローカルアドレス
probe() {
    local where="$1" role="$2" local_addr="$3" output
    output="$(ns "${where}" "${WORK_DIR}/punchprobe" -server "${SERVER_ADDR}:${SERVER_PORT}" \
        -rendezvous "${RENDEZVOUS_ID}" -role "${role}" -local "${local_addr}" \
        -timeout "${PROBE_TIMEOUT}" 2>&1 || true)"
    printf '%s\n' "${output}" >>"${WORK_DIR}/probes.log"
    awk '/^RESULT/ {print $2}' <<<"${output}"
}

# 1 つの組み合わせを試す。
# $1: 子機側の NAT（none は親機を経由せず直結の public に置く）、$2: 親機側の NAT、
# $3: 期待する子機の結果、$4: 期待する親機の結果、$5: 状況（normal、no-server、no-host）
FAILURES=0
run_case() {
    local client_nat="$1" host_nat="$2" want_client="$3" want_host="$4" situation="${5:-normal}"
    setup_topology
    configure_nat nat-a "${client_nat}" 10.0.1.2
    local host_ns="host-b" host_local="10.0.2.2:40000"
    if [[ "${host_nat}" == "public" ]]; then
        host_ns="public"
        host_local="203.0.113.10:40000"
    else
        configure_nat nat-b "${host_nat}" 10.0.2.2
    fi
    if [[ "${situation}" != "no-server" ]]; then
        start_server
    fi
    local client_result host_result="-"
    if [[ "${situation}" == "no-host" ]]; then
        client_result="$(probe host-a client 10.0.1.2:40000)"
    else
        local host_file="${WORK_DIR}/host-result"
        probe "${host_ns}" host "${host_local}" >"${host_file}" &
        local host_pid=$!
        client_result="$(probe host-a client 10.0.1.2:40000)"
        wait "${host_pid}" || true
        host_result="$(cat "${host_file}")"
    fi
    stop_server
    local verdict="ok"
    if [[ "${client_result}" != "${want_client}" || "${host_result}" != "${want_host}" ]]; then
        verdict="FAIL"
        FAILURES=$((FAILURES + 1))
    fi
    printf '%-16s %-16s %-10s %-24s %-24s %s\n' "${client_nat}" "${host_nat}" "${situation}" \
        "${client_result:-none}" "${host_result:-none}" "${verdict}"
}

main() {
    if [[ "$(id -u)" -ne 0 ]]; then
        log "run as root (network namespaces and iptables need it)"
        exit 2
    fi
    mkdir -p "${WORK_DIR}"
    : >"${WORK_DIR}/probes.log"
    trap 'stop_server; teardown' EXIT
    build_binaries
    printf '%-16s %-16s %-10s %-24s %-24s %s\n' "client NAT" "host NAT" "situation" "client" "host" "verdict"
    run_case full-cone       full-cone       CONNECTED   CONNECTED
    run_case port-restricted port-restricted CONNECTED   CONNECTED
    run_case port-restricted full-cone       CONNECTED   CONNECTED
    run_case symmetric       full-cone       CONNECTED   CONNECTED
    run_case symmetric       public          CONNECTED   CONNECTED
    # 対称型と、相手のアドレスとポートで絞る NAT の組み合わせは、中継なしでは越えられない。
    # 「接続済み」とせず UNREACHABLE になることを確かめる。
    run_case symmetric       port-restricted UNREACHABLE UNREACHABLE
    run_case symmetric       symmetric       UNREACHABLE UNREACHABLE
    run_case port-restricted port-restricted RENDEZVOUS_UNREACHABLE RENDEZVOUS_UNREACHABLE no-server
    run_case port-restricted port-restricted PEER_NOT_FOUND - no-host
    if [[ "${FAILURES}" -ne 0 ]]; then
        log "${FAILURES} case(s) did not match; probe output is in ${WORK_DIR}/probes.log"
        cat "${WORK_DIR}/probes.log" >&2
        exit 1
    fi
    log "all cases matched"
}

main "$@"
