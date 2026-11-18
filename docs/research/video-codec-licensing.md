# 映像コーデックと配信方式の特許・ライセンスの調査

## この文書について

Issue #135 の調査です。Issue #136 の ADR で方式を選ぶ材料にします。VOCROZ は Windows 向けインストーラーで AGPL-3.0-only のバイナリを配布します。以下は 2026-11-19 時点の資料の要約です。法的判断は専門家と権利者に確認してください。

## 調べ方

各実装の許諾文と公式の配布資料を読みました。次にプールと規格団体の資料で特許許諾の対象を確認しました。Windows の機能は Microsoft の API 文書、ライセンスの組み合わせは FSF の解説と照合しました。

## 映像コーデック

| 候補 | ソフトウェアのライセンス | AGPL-3.0-only との組み合わせ | 特許 | 配布の条件と注意 | 出典 |
| --- | --- | --- | --- | --- | --- |
| H.264・OpenH264 [1] [2] [26] | BSD-2-Clause [1] [2] [26] | 出典上は可。表示義務を伴う許諾を FSF の BSD 系の説明と照合しました。 [1] [2] [26] | Via LA は AVC 必須特許を対象にしています。自社配布への適用は要確認です。 [1] [2] [26] | Cisco 提供バイナリには利用者側での別途取得などの条件があります。自前ビルドへの特許許諾は要確認です。 [1] [2] [26] | [1] [2] [26] |
| H.264・x264 [2] [3] [26] | GPL-2.0-or-later [2] [3] [26] | 出典上は可。GPLv3 を選ぶと AGPLv3 との結合条項があります。 [2] [3] [26] | Via LA は AVC 必須特許を対象にしています。自社配布への適用は要確認です。 [2] [3] [26] | 選択した GPL の条件に従い対応するソースを提供します。 [2] [3] [26] | [2] [3] [26] |
| HEVC・x265 [4] [5] [26] | GPL-2.0-or-later [4] [5] [26] | 出典上は可。ヘッダーが認める GPLv3 を選べます。 [4] [5] [26] | HEVC Advance は実装製品を対象にしています。製品区分と対象地域は要確認です。 [4] [5] [26] | GPLv3 での配布には対応するソースが必要です。 [4] [5] [26] | [4] [5] [26] |
| AV1・libaom [6] [7] [26] | BSD-2-Clause [6] [7] [26] | 出典上は可。表示義務を伴う許諾を FSF の BSD 系の説明と照合しました。 [6] [7] [26] | AOMedia の許諾は参加者の Necessary Claims が対象です。終了条項があります。 [6] [7] [26] | バイナリに添える文書に著作権表示、条件、免責条項を収録します。 [6] [7] [26] | [6] [7] [26] |
| AV1・SVT-AV1 [6] [8] [26] | BSD-3-Clause-Clear [6] [8] [26] | 出典上は可。表示義務を伴う許諾を FSF の BSD 系の説明と照合しました。 [6] [8] [26] | AOMedia の許諾は参加者の Necessary Claims が対象です。終了条項があります。 [6] [8] [26] | バイナリに添える文書に著作権表示、条件、免責条項を収録します。 ソフトウェアの許諾自体には特許許諾がありません。 [6] [8] [26] | [6] [8] [26] |
| AV1・rav1e [6] [9] [26] | BSD-2-Clause [6] [9] [26] | 出典上は可。表示義務を伴う許諾を FSF の BSD 系の説明と照合しました。 [6] [9] [26] | AOMedia の許諾は参加者の Necessary Claims が対象です。終了条項があります。 [6] [9] [26] | バイナリに添える文書に著作権表示、条件、免責条項を収録します。 [6] [9] [26] | [6] [9] [26] |
| VP9・libvpx [10] [11] [26] | BSD-3-Clause [10] [11] [26] | 出典上は可。表示義務を伴う許諾を FSF の BSD 系の説明と照合しました。 [10] [11] [26] | Google の許諾は対象実装で必然的に侵害する請求項に限られます。 [10] [11] [26] | バイナリに添える文書に著作権表示、条件、免責条項を収録します。 [10] [11] [26] | [10] [11] [26] |
| H.264・Media Foundation [2] [12] [14] | OS 付属（同梱しない） [2] [12] [14] | 要確認。API 資料は VOCROZ の配布条件を規定していません。 [2] [12] [14] | Via LA は AVC 必須特許を対象にしています。自社配布への適用は要確認です。 [2] [12] [14] | 対象 PC でエンコーダー MFT を列挙します。エディション、拡張機能、GPU 別の提供条件は要確認です。 [2] [12] [14] | [2] [12] [14] |
| HEVC・Media Foundation [5] [13] [14] | OS 付属（同梱しない） [5] [13] [14] | 要確認。API 資料は VOCROZ の配布条件を規定していません。 [5] [13] [14] | HEVC Advance は実装製品を対象にしています。製品区分と対象地域は要確認です。 [5] [13] [14] | 対象 PC でエンコーダー MFT を列挙します。エディション、拡張機能、GPU 別の提供条件は要確認です。 [5] [13] [14] | [5] [13] [14] |
| AV1・Media Foundation [6] [14] | OS 付属（同梱しない） [6] [14] | 要確認。API 資料は VOCROZ の配布条件を規定していません。 [6] [14] | AOMedia の許諾は参加者の Necessary Claims が対象です。終了条項があります。 [6] [14] | 対象 PC でエンコーダー MFT を列挙します。エディション、拡張機能、GPU 別の提供条件は要確認です。 [6] [14] | [6] [14] |
| VP9・Media Foundation [11] [14] | OS 付属（同梱しない） [11] [14] | 要確認。API 資料は VOCROZ の配布条件を規定していません。 [11] [14] | Google の許諾は対象実装で必然的に侵害する請求項に限られます。 [11] [14] | 対象 PC でエンコーダー MFT を列挙します。エディション、拡張機能、GPU 別の提供条件は要確認です。 [11] [14] | [11] [14] |

ライブラリを同梱する場合は各実装の再配布条項が適用されます [1] [7]。Cisco のバイナリに対する AVC 許諾には取得方法の指定があります [1]。Media Foundation ではインストール済みの MFT を列挙できます [14]。その API 資料から配布者の特許条件までは確認できません。

## コンテナ

| 候補 | ソフトウェアのライセンス | AGPL-3.0-only との組み合わせ | 特許 | 配布の条件と注意 | 出典 |
| --- | --- | --- | --- | --- | --- |
| MP4 [15] | 規格文書。実装のライセンスは要確認です。 [15] | 要確認。規格文書と読み書きの実装は別に確認します。 [15] | 要確認 | ISO BMFF の初期化部分を W3C が説明しています。 [15] | [15] |
| fragmented MP4 [15] | 規格文書。実装のライセンスは要確認です。 [15] | 要確認。規格文書と読み書きの実装は別に確認します。 [15] | 要確認 | W3C は moof と mdat を含むメディアセグメントを定義しています。 [15] | [15] |
| Matroska [16] | 規格文書。実装のライセンスは要確認です。 [16] | 要確認。規格文書と読み書きの実装は別に確認します。 [16] | 要確認 | RFC 9559 が要素と格納形式を定義しています。 [16] | [16] |
| MP4・Sink Writer [17] | OS 付属（同梱しない） [17] | 要確認。OS 側の sink を呼びます。 [17] | 要確認 | Microsoft の MPEG-4 File Sink は MP4 の書き出しに対応します。 [17] | [17] |

## 外部出力の方式

| 候補 | ソフトウェアのライセンス | AGPL-3.0-only との組み合わせ | 特許 | 配布の条件と注意 | 出典 |
| --- | --- | --- | --- | --- | --- |
| SRT・プロトコル仕様 [18] | Haivision が公開する転送方式です。 [18] | 要確認。仕様の利用条件を権利者に確認します。 [18] | 要確認 | 要確認 | [18] |
| libsrt [18] [19] | MPL-2.0 [18] [19] | 要確認。Secondary Licenses 条項の適用条件を配布対象で確認します。 [18] [19] | 寄与者の特許許諾に範囲と終了条件があります。 [18] [19] | 対象ファイルのソースを MPL の条件で提供します。 [18] [19] | [18] [19] |
| RTMP・プロトコル仕様 [20] | 仕様本文は要確認です。 [20] | 要確認。仕様の利用条件を権利者に確認します。 [20] | 要確認 | 要確認 | [20] |
| librtmp [21] | LGPL（公式ページの表記） [21] | 要確認。配布版の LGPL の版とリンク方法を確認します。 [21] | 要確認 | 静的版・共有版があります。配布方法別の義務は要確認です。 [21] | [21] |
| RTP / SRTP・プロトコル仕様 [22] [23] | RFC 3550 と RFC 3711 がパケット形式と保護方式を定めます。 [22] [23] | 要確認。仕様の利用条件を権利者に確認します。 [22] [23] | 要確認 | 要確認 | [22] [23] |
| libsrtp 2.7.0 [24] [26] | BSD-3-Clause [24] [26] | 出典上は可。表示義務を伴う許諾を FSF の BSD 系の説明と照合しました。 [24] [26] | 要確認 | バイナリに添える文書に著作権表示、条件、免責条項を収録します。 VOCROZ の CONTRIBUTING.md と ADR-0040 の記載に一致します。 [24] [26] | [24] [26] |
| NDI・プロトコル仕様 [25] | SDK の入手を NDI が案内しています。 [25] | 要確認。仕様の利用条件を権利者に確認します。 [25] | 要確認 | 要確認 | [25] |
| NDI SDK [25] | NDI 独自許諾（契約本文は要確認） [25] | 要確認。公開案内は AGPL での再配布許諾を示していません。 [25] | 要確認 | 想定売上に応じて SDK のライセンスについて連絡する場合があるとしています。 [25] | [25] |

## 要確認の点

- AVC・HEVC は配布地域、製品区分、OS ベンダーの契約がアプリ配布者をカバーするかを各プールに確認します。
- AV1・VP9 は許諾対象外の第三者特許と実装別の対象範囲を権利者に確認します。
- 各 MFT の Windows エディション・拡張・GPU 別の供給と再配布条件を Microsoft および GPU ベンダーに確認します。
- コンテナの特許と実装ライブラリの再配布条件を規格団体・実装の権利者に確認します。
- SRT のプロトコルの利用条件と MPL の適用範囲を Haivision に確認します。
- RTMP 仕様の特許と配布条件は仕様の権利者に確認します。https://rtmp.veriskope.com/docs/spec/ と https://rtmp.veriskope.com/pdf/rtmp_specification_1.0.pdf は HTTP 403、https://www.adobe.com/content/dam/acom/en/devnet/rtmp/pdf/rtmp_specification_1.0.pdf は HTTP 404 でした。
- librtmp の対象版の LGPL 条件は開発元に確認します。
- RTP / SRTP の仕様・実装の特許については IETF の開示記録と各権利者に確認します。
- NDI の仕様、特許、SDK の再配布契約と AGPL の組み合わせを NDI と専門家に確認します。

## 出典

1. Cisco, OpenH264 binary license, https://www.openh264.org/BINARY_LICENSE.txt, 2026-11-19 に確認
2. Via Licensing Alliance, AVC/H.264, https://www.via-la.com/licensing-2/avc-h-264/, 2026-11-19 に確認
3. x264 project, x264.h, https://raw.githubusercontent.com/mirror/x264/master/x264.h, 2026-11-19 に確認
4. MulticoreWare, x265 public header, https://bitbucket.org/multicoreware/x265_git/raw/master/source/x265.h, 2026-11-19 に確認
5. Access Advance, HEVC Advance Patent Pool, https://accessadvance.com/hevc-advance-patent-pool/, 2026-11-19 に確認
6. AOMedia, Alliance for Open Media Patent License 1.0, https://aomedia.org/license/patent-license/, 2026-11-19 に確認
7. AOMedia, libaom LICENSE, https://aomedia.googlesource.com/aom/+/refs/heads/main/LICENSE?format=TEXT, 2026-11-19 に確認
8. AOMedia, SVT-AV1 LICENSE, https://gitlab.com/AOMediaCodec/SVT-AV1/-/raw/master/LICENSE.md, 2026-11-19 に確認
9. Xiph.Org, rav1e LICENSE, https://raw.githubusercontent.com/xiph/rav1e/master/LICENSE, 2026-11-19 に確認
10. WebM Project, libvpx LICENSE, https://chromium.googlesource.com/webm/libvpx/+/refs/heads/main/LICENSE?format=TEXT, 2026-11-19 に確認
11. WebM Project, Additional IP Rights Grant, https://www.webmproject.org/license/additional/, 2026-11-19 に確認
12. Microsoft, H.264 Video Encoder, https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder, 2026-11-19 に確認
13. Microsoft, H.265 / HEVC Video Encoder, https://learn.microsoft.com/en-us/windows/win32/medfound/h-265---hevc-video-encoder, 2026-11-19 に確認
14. Microsoft, MFTEnumEx function, https://learn.microsoft.com/en-us/windows/win32/api/mfapi/nf-mfapi-mftenumex, 2026-11-19 に確認
15. W3C, ISO BMFF Byte Stream Format, https://www.w3.org/TR/mse-byte-stream-format-isobmff/, 2026-11-19 に確認
16. IETF, RFC 9559 Matroska Media Container Format Specifications, https://www.rfc-editor.org/rfc/rfc9559.html, 2026-11-19 に確認
17. Microsoft, MPEG-4 File Sink, https://learn.microsoft.com/en-us/windows/win32/medfound/mpeg-4-file-sink, 2026-11-19 に確認
18. Haivision, SRT protocol and library, https://github.com/Haivision/srt, 2026-11-19 に確認
19. Haivision, SRT LICENSE, https://raw.githubusercontent.com/Haivision/srt/master/LICENSE, 2026-11-19 に確認
20. RTMPDump project, RTMPDump（対応するプロトコル）, https://rtmpdump.mplayerhq.hu/, 2026-11-19 に確認
21. RTMPDump project, RTMPDump, https://rtmpdump.mplayerhq.hu/, 2026-11-19 に確認
22. IETF, RFC 3550 RTP, https://www.rfc-editor.org/rfc/rfc3550, 2026-11-19 に確認
23. IETF, RFC 3711 SRTP, https://www.rfc-editor.org/rfc/rfc3711, 2026-11-19 に確認
24. Cisco, libSRTP v2.7.0 LICENSE, https://raw.githubusercontent.com/cisco/libsrtp/v2.7.0/LICENSE, 2026-11-19 に確認
25. NDI, NDI SDK download, https://ndi.video/for-developers/ndi-sdk/download/, 2026-11-19 に確認
26. Free Software Foundation, Various Licenses and Comments about Them, https://www.gnu.org/licenses/license-list.html, 2026-11-19 に確認
