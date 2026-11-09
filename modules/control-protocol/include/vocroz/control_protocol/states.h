// SPDX-License-Identifier: AGPL-3.0-only
//
// UI とエンジン間でやり取りする状態とエラーコードの列挙。
// 状態は互いに独立した軸（セッション、接続、信号、出力デバイス、子機）に分け、1
// つの列挙に混ぜない。 JSON では仕様書の正規名（UPPER_SNAKE_CASE）の文字列で表す。UI
// は正規名を表示せず、表示用ラベルに変換する。
#pragma once

namespace vocroz::control_protocol {

/// 親機のセッション状態。
/// 遷移は IDLE → CREATING → READY ⇄ ACTIVE → ENDING → IDLE。CREATING、READY、ACTIVE からは ERROR
/// へ移り、 ERROR からは IDLE へ戻る。
enum class SessionState {
    kIdle,      ///< IDLE: セッションなし
    kCreating,  ///< CREATING: ランデブーへの登録と鍵の生成中
    kReady,     ///< READY: 作成済みで、参加者なし
    kActive,    ///< ACTIVE: 1 人以上が CONNECTED か RECONNECTING
    kEnding,    ///< ENDING: 終了処理中
    kError,     ///< ERROR: 致命的なエラー。原因は SessionInfo::error_code
};

/// 親機側から見た参加者の接続状態。
enum class ConnectionState {
    kPending,  ///< PENDING: 承認待ち。ストリップは持たず、承認パネルにだけ出る
    kConnecting,    ///< CONNECTING: 承認済みで、経路の確立と鍵交換の実行中
    kConnected,     ///< CONNECTED: 暗号化セッションと制御チャネルが確立済み
    kReconnecting,  ///< RECONNECTING: 受信が途絶え、再接続の猶予中
    kLost,    ///< LOST: 再接続の猶予切れ。ストリップは位置を保ったまま残る
    kFailed,  ///< FAILED: 経路を確立できなかった。原因は ChannelStrip::error_code
};

/// 信号状態。接続状態が CONNECTED のときだけ値を持つ。
enum class SignalState {
    kReceiving,  ///< RECEIVING: 有効な音声フレームを受信中（親機のチャンネルミュート中も含む）
    kPaused,     ///< PAUSED: 子機の送信ミュートで意図的に止まっている
    kResuming,  ///< RESUMING: 送信ミュートの解除後、最初のフレームを待っている
    kNoSignal,  ///< NO_SIGNAL: 送信中のはずのフレームが届かず、無音にしている
};

/// 親機の出力デバイスの状態。
enum class OutputDeviceState {
    kNone,  ///< NONE: 出力デバイスが未選択
    kOpen,  ///< OPEN: 選択したデバイスへ出力中
    kLost,  ///< LOST: 使用中のデバイスが消えた。Master は SAFETY
            ///< でミュートされ、他のデバイスへは切り替えない
};

/// Master をミュートした主体。
enum class MasterMuteReason {
    kOperator,  ///< OPERATOR: オペレーターの操作
    kSafety,    ///< SAFETY:
              ///< 出力デバイスの消失などでエンジンが安全のために設定した。解除はオペレーターだけができる
};

/// 子機の接続状態。
enum class ClientState {
    kDisconnected,  ///< DISCONNECTED: 未接続。直前の終了理由を ClientSnapshot::failure_code
                    ///< に持てる
    kResolving,        ///< RESOLVING: 招待コードをランデブーサーバーで解決中
    kAuthenticating,   ///< AUTHENTICATING: 参加要求の送信と認証
    kWaitingApproval,  ///< WAITING_APPROVAL: 親機の承認待ち
    kConnecting,       ///< CONNECTING: 承認後、経路の確立と鍵交換の実行中
    kConnected,        ///< CONNECTED: 接続済み
    kReconnecting,     ///< RECONNECTING: 受信が途絶え、再接続を試行中
    kFailed,           ///< FAILED: 終端的な失敗。自動では再試行しない
};

/// エンジンが返すエラーコード。UI はこのコードからローカライズ済みのメッセージを表示する。
/// 子機の FAILED と DISCONNECTED
/// の理由、セッションとストリップの失敗の原因、コマンドの拒否理由に共通で使う。
enum class ErrorCode {
    kRejected,          ///< REJECTED: 親機が参加を承認しなかった
    kInviteExpired,     ///< INVITE_EXPIRED: 招待の有効期限切れ
    kInviteRevoked,     ///< INVITE_REVOKED: 招待が取り消された
    kInviteInvalid,     ///< INVITE_INVALID: 招待コードか URL の形式が正しくない
    kUnreachable,       ///< UNREACHABLE: NAT 越えが成立しなかった
    kReconnectTimeout,  ///< RECONNECT_TIMEOUT: 再接続の猶予切れ
    kSessionEnded,      ///< SESSION_ENDED: 親機がセッションを終了した
    kRevisionConflict,  ///< REVISION_CONFLICT: コマンドの expected_revision が entity の revision
                        ///< と違う
    kNotFound,      ///< NOT_FOUND: コマンドの対象の entity がない
    kInvalidState,  ///< INVALID_STATE: 今の状態ではそのコマンドを適用できない
    kEngineError,   ///< ENGINE_ERROR: エンジン内部のエラー
};

/// エンジンからの通知の重要度。
enum class NoticeSeverity {
    kInfo,     ///< info: 4 秒で消えるトースト
    kWarning,  ///< warning: 8 秒表示する警告
};

/// ルーティングのバスの種類。処理は同じで、ルーティング画面での表示と既定の送り方だけが違う。
enum class BusKind {
    kUser,  ///< USER: ユーザーバス（サブグループ、エフェクトの送りなど）
    kCue,  ///< CUE: キューやモニターのバス（出演者への返しや、オペレーターのモニター）
};

/// センドを取り出す位置。
enum class SendTap {
    kPostFader,  ///< POST_FADER: 送り元のフェーダーとミュートの後
    /// PRE_FADER: 送り元のフェーダーとミュートの前（モニターの返しなど）。
    /// 送り元のミュートでは止まらないので、センドのミュートで止める。
    kPreFader,
};

/// ステージングしたルーティングを適用できない理由。最初に見つかった 1 つだけを返す。
enum class RoutingIssue {
    kInvalidId,  ///< INVALID_ID: entity_id が種類ごとの接頭辞（bus- など）で始まらない
    kDuplicateId,   ///< DUPLICATE_ID: 同じ entity_id が複数ある
    kTooManyItems,  ///< TOO_MANY_ITEMS: バス、出力、センド、遅延ノードのいずれかが上限を超える
    kUnknownNode,  ///< UNKNOWN_NODE: センドや手動ディレイが存在しないノードを指している
    kInvalidSend,  ///< INVALID_SEND:
                   ///< つなげない組み合わせ（ストリップへの入力、出力からの送りなど）か、重複
    kInvalidSendTap,  ///< INVALID_SEND_TAP: プリフェーダーの送り元がストリップでもバスでもない
    kDelayOutOfRange,  ///< DELAY_OUT_OF_RANGE: 手動ディレイか遅延ノードの遅延が範囲外
    kOutputChannelOutOfRange,  ///< OUTPUT_CHANNEL_OUT_OF_RANGE: 出力のチャンネルが範囲外
    kOverlappingOutputs,  ///< OVERLAPPING_OUTPUTS: 複数の出力が同じデバイスのチャンネルに書く
    kUndelayedLoop,  ///< UNDELAYED_LOOP: 遅延ノードを通らないループがある。承認しても適用できない
};

/// 子機の入力の種類。ストリップの表示（マイク、アプリ音声）に使う。
enum class InputSourceKind {
    kMicrophone,  ///< MICROPHONE: 入力デバイス（マイク、ライン入力、オーディオインターフェース）
    kApplicationAudio,  ///< APP_AUDIO: PC 内のアプリ音声（WASAPI のループバック）
};

}  // namespace vocroz::control_protocol
