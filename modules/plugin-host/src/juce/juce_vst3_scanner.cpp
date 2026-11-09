// SPDX-License-Identifier: AGPL-3.0-only
//
// scan_in_process()（factories.h）の実装。JUCE の VST3PluginFormatHeadless でファイル 1
// つを調べる。 プラグインのコードをこのプロセスで実行するので、pa-plugin-scanner の中でだけ呼ぶ。

#include <memory>

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include "vocroz/plugin_host/factories.h"

#include "juce_plugin_description.h"

namespace vocroz::plugin_host {

ScanReport scan_in_process(const std::filesystem::path& plugin_file) {
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> found;
    // u8string() で UTF-8 にしてから渡す。Windows の既定のコードページに依存しないため。
    const auto utf8 = plugin_file.u8string();
    const juce::String path = juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.c_str()),
                                                     static_cast<int>(utf8.size()));
    format.findAllTypesForFile(found, path);

    ScanReport report;
    for (const juce::PluginDescription* description : found) {
        report.plugins.push_back(to_plugin_description(*description));
    }
    return report;
}

}  // namespace vocroz::plugin_host
