// SPDX-License-Identifier: AGPL-3.0-only
// フェイクエンジンの録音とメディアプレイヤー。pa-engine の session（MediaDesk と HostCommandHandler）を
// 簡略化して、録音の設定、開始と停止、ファイルの読み込み、再生、シーク、流す先の変更を再現する。
// 規則の正本は C++ 側（modules/session/src/media_desk.cpp、
// docs/adr/0043-recording-and-media-player.md）で、ここは画面の開発と手動の確認のための写しである。
// ファイルは作らず、録音を止めたらテイクのファイルをライブラリの一覧に足すだけにする。

import type {
  ErrorCode,
  MediaFile,
  MediaPlayer,
  MediaTransport,
  Recorder,
  RecorderConfig,
} from "../messages";

/** 経過時間を知らせる間隔（ms）。エンジンと同じく 1 秒ごとにする。 */
const kElapsedIntervalMs = 1000;
/** 再生位置を知らせる間隔（ms）。エンジンと同じく 250 ms ごとにする。 */
const kPositionIntervalMs = 250;
/** フェイクのファイルの長さ（ms）。シークバーを動かして確かめられる長さにする。 */
const kFakeDurationMs = 3 * 60 * 1000;
/** ファイル名に使えない文字。Windows のファイル名の規則に合わせる。 */
const kUnsafeFileNameChars = /[\\/:*?"<>|]/g;

/** 起動時のライブラリ。録音したテイクと、持ち込んだ音源の例を置く。 */
const kInitialLibrary: MediaFile[] = [
  { path: "20261108-193000/01-Master.wav" },
  { path: "bgm/opening.flac" },
  { path: "bgm/ending.wav" },
];

/** コマンドの適用の結果。 */
export type MediaChange = { ok: true; revision: number } | { ok: false; error: ErrorCode };

/** 時間の経過で変わったもの。 */
export interface MediaTick {
  recorder: boolean;
  player: boolean;
  /** ファイルの終わりまで再生して止まった。 */
  ended: boolean;
}

/** フェイクエンジンの録音とメディアプレイヤー。 */
export class FakeMedia {
  private recorderState: Recorder;
  private playerState: MediaPlayer;
  private files: MediaFile[] = [...kInitialLibrary];
  private sinceElapsedMs = 0;
  private sincePositionMs = 0;
  private nextTake = 1;

  constructor(recorderId: string, playerId: string) {
    this.recorderState = {
      entity_id: recorderId,
      revision: 0,
      config: { format: "WAV", folder: "C:\\Users\\pa\\Music\\VOCROZ", sources: ["master"] },
      // 起動時は録音していない。
      state: "STOPPED",
      take: null,
      elapsed_ms: 0,
      tracks: [],
      last_result: null,
    };
    this.playerState = {
      entity_id: playerId,
      revision: 0,
      file: null,
      channels: 0,
      duration_ms: 0,
      target: "master",
      // 起動時は止まっている（初期ミュート）。
      transport: "STOPPED",
      position_ms: 0,
      last_result: null,
    };
  }

  recorder(): Recorder {
    return this.recorderState;
  }

  player(): MediaPlayer {
    return this.playerState;
  }

  library(): MediaFile[] {
    return this.files;
  }

  /**
   * 録音の設定を変える。録音中は変えられない。
   * @param isSource 録る対象として使える entity か
   */
  setConfig(config: RecorderConfig, isSource: (id: string) => boolean): MediaChange {
    if (this.recorderState.state === "RECORDING") return { ok: false, error: "INVALID_STATE" };
    if (!config.sources.every(isSource)) return { ok: false, error: "NOT_FOUND" };
    // 同じ対象を 2 本録っても同じ音のファイルが増えるだけなので、重複は 1 本にまとめる。
    const next = { ...config, sources: [...new Set(config.sources)] };
    return { ok: true, revision: this.updateRecorder({ config: next }) };
  }

  /**
   * 録音を開始か停止する。
   * @param labelOf 録る対象の表示名（ファイル名に使う）
   * @param diskFull 開始の時点で空き容量が足りない状況を再現するか
   */
  setRecording(
    recording: boolean,
    labelOf: (id: string) => string,
    diskFull: boolean,
  ): MediaChange {
    const current = this.recorderState;
    if (recording === (current.state === "RECORDING")) {
      return { ok: true, revision: current.revision };
    }
    if (!recording) return { ok: true, revision: this.stopRecording("OK") };
    if (current.config.sources.length === 0) return { ok: false, error: "INVALID_STATE" };
    if (diskFull) {
      // ファイルを開けなかったときは、コマンドを受け付けたうえで止まったまま結果で知らせる。
      return { ok: true, revision: this.updateRecorder({ last_result: "DISK_FULL", tracks: [] }) };
    }
    const extension = current.config.format === "FLAC" ? "flac" : "wav";
    const take = `20261115-2000${String(this.nextTake++).padStart(2, "0")}`;
    const tracks = current.config.sources.map((id, i) => ({
      source_id: id,
      file_name: `${String(i + 1).padStart(2, "0")}-${labelOf(id).replace(kUnsafeFileNameChars, "_")}.${extension}`,
      dropped_ms: 0,
    }));
    this.sinceElapsedMs = 0;
    return {
      ok: true,
      revision: this.updateRecorder({
        state: "RECORDING",
        take,
        elapsed_ms: 0,
        tracks,
        last_result: "OK",
      }),
    };
  }

  /** 録音中にディスクがいっぱいになったことを再現する。録音を止め、書けたところまでを残す。 */
  failRecording(): boolean {
    if (this.recorderState.state !== "RECORDING") return false;
    this.stopRecording("DISK_FULL");
    return true;
  }

  /** ディスクの書き込みが間に合わなかったことを再現する。欠落は観測値なので revision を進めない。 */
  dropFrames(ms: number): boolean {
    if (this.recorderState.state !== "RECORDING") return false;
    const tracks = this.recorderState.tracks.map((t) => ({ ...t, dropped_ms: t.dropped_ms + ms }));
    this.updateRecorder({ tracks }, false);
    return true;
  }

  /** ファイルを読み込む。空のパスはファイルを外す。ライブラリにないパスは受け付けない。 */
  load(path: string): MediaChange {
    if (path === "") {
      return {
        ok: true,
        revision: this.updatePlayer({
          file: null,
          channels: 0,
          duration_ms: 0,
          transport: "STOPPED",
          position_ms: 0,
        }),
      };
    }
    if (!this.files.some((f) => f.path === path)) return { ok: false, error: "NOT_FOUND" };
    // 読み込んだ直後は止まっていて、再生を押すまで鳴らない（初期ミュート）。
    return {
      ok: true,
      revision: this.updatePlayer({
        file: path,
        channels: 2,
        duration_ms: kFakeDurationMs,
        transport: "STOPPED",
        position_ms: 0,
        last_result: "OK",
      }),
    };
  }

  /**
   * 再生、一時停止、停止する。停止は一時停止と先頭へのシーク。
   * @param targetExists 流す先がいまのルーティングにあるか
   */
  setTransport(transport: MediaTransport, targetExists: boolean): MediaChange {
    if (this.playerState.file === null) return { ok: false, error: "INVALID_STATE" };
    if (transport === "PLAYING" && !targetExists) return { ok: false, error: "INVALID_STATE" };
    this.sincePositionMs = 0;
    return {
      ok: true,
      revision: this.updatePlayer(
        transport === "STOPPED" ? { transport, position_ms: 0 } : { transport },
      ),
    };
  }

  /** 再生位置を移す。長さを超える位置は終わりとして扱う。 */
  seek(positionMs: number): MediaChange {
    if (this.playerState.file === null) return { ok: false, error: "INVALID_STATE" };
    const position = Math.min(Math.max(0, positionMs), this.playerState.duration_ms);
    return { ok: true, revision: this.updatePlayer({ position_ms: position }) };
  }

  /**
   * 流す先を変える。
   * @param isTarget 流す先として使える entity か（Master か適用中のバス）
   */
  setTarget(target: string, isTarget: (id: string) => boolean): MediaChange {
    if (!isTarget(target)) return { ok: false, error: "NOT_FOUND" };
    return { ok: true, revision: this.updatePlayer({ target }) };
  }

  /**
   * ルーティングを適用した後に呼ぶ。再生中に流す先のバスがなくなったら止める。別のバスや Master へ
   * 暗黙に切り替えると、意図しない出力に音が出るため。
   * @returns 止めたか
   */
  onRoutingApplied(isTarget: (id: string) => boolean): boolean {
    if (this.playerState.transport !== "PLAYING" || isTarget(this.playerState.target)) return false;
    this.updatePlayer({ transport: "PAUSED" });
    return true;
  }

  /** 録る対象からなくなったストリップやバスを外す。 */
  pruneSources(isSource: (id: string) => boolean): boolean {
    const sources = this.recorderState.config.sources.filter(isSource);
    if (sources.length === this.recorderState.config.sources.length) return false;
    this.updateRecorder({ config: { ...this.recorderState.config, sources } });
    return true;
  }

  /** 時間を進める。経過時間と再生位置は観測値なので revision を進めない。 */
  tick(intervalMs: number): MediaTick {
    const result: MediaTick = { recorder: false, player: false, ended: false };
    if (this.recorderState.state === "RECORDING") {
      this.sinceElapsedMs += intervalMs;
      if (this.sinceElapsedMs >= kElapsedIntervalMs) {
        this.updateRecorder(
          { elapsed_ms: this.recorderState.elapsed_ms + this.sinceElapsedMs },
          false,
        );
        this.sinceElapsedMs = 0;
        result.recorder = true;
      }
    }
    if (this.playerState.transport === "PLAYING") {
      this.sincePositionMs += intervalMs;
      if (this.sincePositionMs >= kPositionIntervalMs) {
        const position = this.playerState.position_ms + this.sincePositionMs;
        this.sincePositionMs = 0;
        // 終わりまで鳴らしたら止めて先頭に戻す。エンジンが決めたことなので revision は進めない。
        if (position >= this.playerState.duration_ms) {
          this.updatePlayer({ transport: "STOPPED", position_ms: 0 }, false);
          result.ended = true;
        } else {
          this.updatePlayer({ position_ms: position }, false);
        }
        result.player = true;
      }
    }
    return result;
  }

  private stopRecording(result: "OK" | "DISK_FULL"): number {
    const { take, tracks } = this.recorderState;
    // 録り終えたテイクはすぐに再生して確かめられるよう、ライブラリに足す。
    if (take !== null) {
      const added = tracks.map((t) => ({ path: `${take}/${t.file_name}` }));
      this.files = [...this.files, ...added].sort((a, b) => a.path.localeCompare(b.path));
    }
    return this.updateRecorder({ state: "STOPPED", last_result: result });
  }

  private updateRecorder(patch: Partial<Recorder>, bumpRevision = true): number {
    const current = this.recorderState;
    this.recorderState = {
      ...current,
      ...patch,
      revision: current.revision + (bumpRevision ? 1 : 0),
    };
    return this.recorderState.revision;
  }

  private updatePlayer(patch: Partial<MediaPlayer>, bumpRevision = true): number {
    const current = this.playerState;
    this.playerState = {
      ...current,
      ...patch,
      revision: current.revision + (bumpRevision ? 1 : 0),
    };
    return this.playerState.revision;
  }
}
