// SPDX-License-Identifier: AGPL-3.0-only
//
// composition_root.h の実装。

#include "composition_root.h"

#include <string>
#include <system_error>
#include <utility>

#include "vocroz/device/factories.h"
#include "vocroz/ipc/factories.h"
#include "vocroz/network/factories.h"
#include "vocroz/persistence/factories.h"
#include "vocroz/update/session_lock.h"

#include "engine_config.h"
#include "engine_constants.h"
#include "host_key_loader.h"
#include "stderr_logger.h"
#include "win/console_stop_signal.h"
#include "win/known_paths.h"
#include "win/minidump_crash_handler.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;

[[nodiscard]] audio_core::MixerConfig mixer_config() {
    audio_core::MixerConfig config;
    config.fader_capacity = kMixerFaderCapacity;
    config.meter_capacity = kMixerMeterCapacity;
    return config;
}

[[nodiscard]] std::string to_utf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

[[nodiscard]] std::wstring widen_ascii(const std::string& text) {
    // 名前付きミューテックスの名前は 16 進数と英字だけ（session_lock_name()）なので、
    // 1 バイトずつ広げればよい。
    return {text.begin(), text.end()};
}

// パイプの名前（\\.\pipe\vocroz-engine-<SID>-<ハッシュ>）の最後の要素を、単一起動の印の名前に使う。
// パイプと同じ「ユーザー × データフォルダ」の範囲になり、規則を 2 か所に持たずに済む。
[[nodiscard]] std::wstring instance_lock_name(const std::wstring& pipe_name) {
    const auto slash = pipe_name.find_last_of(L'\\');
    return L"Local\\" + pipe_name.substr(slash == std::wstring::npos ? 0 : slash + 1);
}

// --data-dir（なければ %LOCALAPPDATA%\VOCROZ）を絶対パスにそろえて作る。
// パイプとミューテックスの名前はパスから作るので、remote-pa と表記が違っても同じ名前になるように。
// 決められないか作れなければ空を返す。
[[nodiscard]] std::filesystem::path resolve_data_dir(const LaunchOptions& options) {
    const auto base = options.data_dir ? std::optional{std::filesystem::path{std::u8string(
                                             options.data_dir->begin(), options.data_dir->end())}}
                                       : win::default_data_dir();
    if (!base) {
        return {};
    }
    std::error_code error;
    auto absolute = std::filesystem::absolute(*base, error);
    if (error || (std::filesystem::create_directories(absolute, error), error)) {
        return {};
    }
    return absolute;
}

[[nodiscard]] std::unique_ptr<common::Logger> make_logger(const std::filesystem::path& data_dir,
                                                          common::LogLevel level) {
    if (data_dir.empty()) {
        // ログファイルの置き場所がないので、起動できない理由だけでも標準エラー出力に残す。
        return std::make_unique<StderrLogger>(level);
    }
    return persistence::make_rotating_file_logger(
        persistence::LogOptions{.directory = persistence::log_directory(data_dir),
                                .base_name = "pa-engine",
                                .min_level = level});
}

}  // namespace

CompositionRoot::CompositionRoot(const LaunchOptions& options)
    : options_(options),
      data_dir_(resolve_data_dir(options)),
      logger_(make_logger(data_dir_, options.log_level)),
      invite_relay_(loop_),
      offline_rendezvous_(loop_),
      mixer_(mixer_config()),
      engine_mixer_(mixer_, *logger_),
      device_loss_relay_(mixer_, loop_, [this] { on_output_device_lost(); }),
      idle_timer_(kIdleShutdownTimeout) {}

CompositionRoot::~CompositionRoot() {
    shutdown();
}

int CompositionRoot::run() {
    if (const auto failed = start()) {
        return *failed;
    }
    win::install_console_stop_signal(loop_);
    logger_->log(common::LogLevel::kInfo, "エンジンを起動しました");
    loop_.run(kControlTickInterval, [this] { on_tick(); });
    win::uninstall_console_stop_signal();
    shutdown();
    logger_->log(common::LogLevel::kInfo, "エンジンを終了しました");
    return exit_code_;
}

std::optional<int> CompositionRoot::start() {
    if (data_dir_.empty()) {
        logger_->log(common::LogLevel::kError, "データフォルダを決められないか、作れません");
        return kExitFatal;
    }
    win::install_minidump_crash_handler(data_dir_ / L"crash", *logger_);

    const auto pipe_name = ipc::engine_pipe_name(data_dir_);
    if (!pipe_name) {
        logger_->log(common::LogLevel::kError, "パイプの名前を決められません");
        return kExitFatal;
    }
    // 単一起動。同じデータフォルダのエンジンが動いていれば、remote-pa はそちらにつなげばよい。
    auto instance = win::Win32NamedMutex::create(instance_lock_name(pipe_name.value()), true);
    if (!instance) {
        logger_->log(common::LogLevel::kInfo, "同じデータフォルダのエンジンがすでに動いています");
        return instance.error() == win::NamedMutexError::kAlreadyExists ? kExitAlreadyRunning
                                                                        : kExitFatal;
    }
    instance_lock_.emplace(std::move(instance).value());

    // 乱数が使えないと招待トークンと再接続トークンが推測できる値になるので、起動しない。
    if (!CryptoSecureRandom::is_available()) {
        logger_->log(common::LogLevel::kError, "暗号ライブラリを初期化できません");
        return kExitFatal;
    }
    settings_ = persistence::make_json_settings_store(data_dir_, *logger_);
    secrets_ = persistence::make_dpapi_secret_store(data_dir_);
    auto keys = load_host_keys(*secrets_, *logger_);
    if (!keys) {
        logger_->log(common::LogLevel::kError, "親機の鍵を用意できません");
        return kExitFatal;
    }
    host_keys_.emplace(std::move(keys).value());
    create_rendezvous_client();

    auto channel = ipc::make_pipe_server(pipe_name.value(), *logger_);
    if (!channel) {
        logger_->log(common::LogLevel::kError, "UI の待ち受けを始められません");
        return channel.error() == ipc::PipeError::kAlreadyExists ? kExitAlreadyRunning : kExitFatal;
    }
    ui_channel_ = std::move(channel).value();
    device_catalog_ = device::make_wasapi_audio_device_catalog(*logger_);

    ui_publisher_ = std::make_unique<UiEventPublisher>(*ui_channel_, *logger_);
    session::HostSession::Config config;
    config.host_public_key = host_keys_->public_key();
    config.invite_base_url = kInviteBaseUrl;
    session_ = std::make_unique<session::HostSession>(
        std::move(config),
        session::HostSession::Ports{*ui_publisher_, engine_mixer_, peers_, *rendezvous_, *settings_,
                                    random_, clock_, *logger_});
    const auto on_registered =
        [this](
            const session::InviteToken& token,
            const common::Result<session::RegisteredInvite, control_protocol::ErrorCode>& result) {
            session_->on_invite_registered(token, result);
        };
    invite_relay_.set_registration_handler(on_registered);
    offline_rendezvous_.set_registration_handler(on_registered);
    session_->start();
    open_saved_output_device();

    // UI の受け付けは、session が初期状態（全ミュート）になってから始める。
    ui_receiver_ = std::make_unique<UiMessageReceiver>(
        loop_,
        [this](const control_protocol::UiToEngineMessage& message) {
            ui_publisher_->note_ui_message_received();
            session_->handle_ui_message(message);
        },
        *logger_);
    ui_channel_->set_listener(ui_receiver_.get());
    next_session_tick_ = clock_.now();
    return std::nullopt;
}

void CompositionRoot::create_rendezvous_client() {
    const std::string_view url = kInviteBaseUrl;
    if (url.empty()) {
        logger_->log(common::LogLevel::kWarning,
                     "ランデブーサーバーの URL がないため、セッションを作れません");
        return;
    }
    auto transport = network::make_winhttp_https_transport(url);
    if (!transport) {
        logger_->log(common::LogLevel::kWarning,
                     "ランデブーサーバーとの HTTPS 通信を始められないため、セッションを作れません");
        return;
    }
    https_transport_ = std::move(transport).value();
    https_rendezvous_ =
        std::make_unique<network::HttpsRendezvousClient>(*https_transport_, invite_relay_);
    rendezvous_ = https_rendezvous_.get();
}

void CompositionRoot::open_saved_output_device() {
    // 保存した ID のデバイスだけを開く。開けなければ未選択のままにし、ほかのデバイスへは
    // 切り替えない（出力デバイスを暗黙にフォールバックしない）。
    auto saved = settings_->load_host();
    if (!saved || !saved.value().output_device_id) {
        return;
    }
    const std::string& id = *saved.value().output_device_id;
    auto output = device::make_wasapi_audio_output(id, *logger_);
    if (!output) {
        logger_->log(common::LogLevel::kWarning, "保存した出力デバイスを開けませんでした");
        return;
    }
    if (const auto started = output.value()->start(mixer_, device_loss_relay_); !started) {
        logger_->log(common::LogLevel::kWarning,
                     "保存した出力デバイスで出力を始められませんでした");
        return;
    }
    output_ = std::move(output).value();
    std::string name = id;
    if (auto devices = device_catalog_->list(ac::AudioDeviceDirection::kOutput)) {
        for (const auto& device : devices.value()) {
            if (device.id == id) {
                name = device.name;
            }
        }
    }
    session_->on_output_device_opened(id, name);
}

void CompositionRoot::on_output_device_lost() {
    // 安全ミュートは DeviceLossRelay がすでにかけている。ここでは止まったデバイスを片付け、
    // session に Master の SAFETY ミュートと LOST の表示をさせる。
    if (output_) {
        output_->stop();
        output_.reset();
    }
    session_->on_output_device_lost();
    logger_->log(common::LogLevel::kWarning,
                 "出力デバイスが抜かれたため、Master をミュートしました");
}

void CompositionRoot::on_tick() {
    engine_mixer_.collect();
    const auto now = clock_.now();
    if (now >= next_session_tick_) {
        next_session_tick_ = now + kSessionTickInterval;
        session_->tick();
        apply_activity();
    }
    // メーターは UI がつながっていなくても送る。送信の失敗で UI の切断に気付くため
    // （ui_event_publisher.h）。
    ui_publisher_->publish(control_protocol::Meters{engine_mixer_.read_meters()});

    if (idle_timer_.should_exit(now, ui_publisher_->is_ui_connected(), session_exists_)) {
        logger_->log(common::LogLevel::kInfo, "セッションも UI もないため、エンジンを終了します");
        loop_.request_stop();
    }
}

void CompositionRoot::apply_activity() {
    const auto activity = activity_for(session_->snapshot().session.state);
    session_exists_ = activity.session_exists;
    sleep_inhibitor_.set_inhibited(activity.prevent_sleep);
    if (activity.session_exists && !session_lock_) {
        auto lock = win::Win32NamedMutex::create(
            widen_ascii(update::session_lock_name(to_utf8(data_dir_))), false);
        if (lock) {
            session_lock_.emplace(std::move(lock).value());
        } else {
            logger_->log(common::LogLevel::kWarning,
                         "セッション中の印を作れないため、本番中の更新を止められません");
        }
    } else if (!activity.session_exists && session_lock_) {
        session_lock_.reset();
    }
}

void CompositionRoot::shutdown() {
    // UI の受信を先に止め、ループに積まれた処理が session を呼ばないようにする。
    if (ui_channel_) {
        ui_channel_->set_listener(nullptr);
    }
    if (output_) {
        output_->stop();
        output_.reset();
    }
    sleep_inhibitor_.set_inhibited(false);
    session_lock_.reset();
}

}  // namespace vocroz::engine
