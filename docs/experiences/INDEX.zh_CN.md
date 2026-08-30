<p align="right">
  <strong>简体中文</strong> · <a href="INDEX.md">English</a>
</p>

# 开发经验档案索引

本页列出 [`docs/experiences/`](../development/experience-notes.md) 下所有已记录的开发经验条目，
按贡献开发者的 GitHub 用户名分组。每条是发布后可复用的经验，由 `experience-pr` skill 写入并索引。

如何新增条目、哪些内容归属这里，见[经验索引](../development/experience-notes.md)。

## 索引

每条经验保存在 `docs/experiences/<username>/` 下，并在下面按贡献开发者的 GitHub 用户名分组列出。
一位开发者可有**一条或多条**经验；每条都是独立记录，新经验**新增一条**，而不是并入已有条目。

### Shinku-Chen

- [ESP32-C3 上音频压缩方式的权衡](shinku-chen/audio-compression-trade-offs.zh_CN.md) — 在有限 Flash 上如何为语音播放应用选编解码（IMA-ADPCM vs Opus vs MP3），含实测容量与解码器成本。
- [发布后收尾：AI Passport 发布流程的衔接](shinku-chen/post-release-follow-up.zh_CN.md) — 确认发布目的地、发布时包含数据分区、以及发布后收尾各轨道的同意门槛。
- [ESP32-C3（无 PSRAM）上的显示刷新与深睡](shinku-chen/display-refresh-and-deep-sleep.zh_CN.md) — 直接刷新单个图片矩形、RTC GPIO 深睡唤醒，以及 LVGL 对象类型误用的崩溃特征。

### AmazingAndrew

- [CI 构建踩坑：从双 sdkconfig 回归上游简洁模式](amazingandrew/ci-pitfalls.zh_CN.md) — 8 个具体 CI 失败点（可执行权限位、CMake 列表格式、`espressif/esp-idf-ci-action` 引号与多行解析、symlink、ESP-IDF v5.5.3 / GCC 14 / LVGL v9 API 漂移），以及对齐上游 `validate.sh --firmware` 模式的复盘。
