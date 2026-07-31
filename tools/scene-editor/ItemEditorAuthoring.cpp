/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Item authoring dialog (New / Modify Item) + sub-edit text popup.
 ******************************************************************************/

#include "ItemEditor.h"

#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::loadAssetBytesFromFile;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

const float kNewItemHeaderBtnH = 22.0f;
const float kNewItemHeaderBtnW = 78.0f;
const float kEditItemHeaderBtnW = 78.0f;
const float kIdBadgeFont = 12.0f;
const float kIdBadgeHeight = 22.0f;
const float kAuthoringFooterH = 58.0f;
const float kAuthoringHeaderH = 56.0f;
const float kSubEditFont = 16.0f;
const float kSubEditLineH = 20.0f;
const float kSubEditPad = 8.0f;

void appendUtf8Codepoint(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
    if (codepoint < 0x80)
    {
        buffer.push_back(static_cast<char>(codepoint));
        return;
    }
    char bytes[5] = {};
    int size = 0;
    if (codepoint <= 0x7FF)
    {
        bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    }
    else if (codepoint <= 0x10FFFF)
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    for (int i = 0; i < size; ++i)
        buffer.push_back(bytes[i]);
}

void drawEditorButton(Font font, Rectangle bounds, const char* label, bool accent, bool enabled)
{
    const Color fill = !enabled
        ? kButtonDisabled
        : (accent ? kPanelAccent : Color{44, 42, 52, 255});
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, 1.0f, kPanelBorder);
    const Vector2 size = MeasureTextEx(font, label, kFontBody, 1.0f);
    DrawTextEx(
        font,
        label,
        {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + (bounds.height - size.y) * 0.5f},
        kFontBody,
        1.0f,
        enabled ? kTextPrimary : kTextDisabled);
}

void drawCheckboxRow(
    Font font,
    Rectangle row,
    const char* label,
    bool checked,
    bool hovered)
{
    const float box = 16.0f;
    const Rectangle boxRec = {
        row.x,
        row.y + (row.height - box) * 0.5f,
        box,
        box};
    DrawRectangleRec(boxRec, hovered ? Color{50, 46, 58, 255} : Color{36, 34, 44, 255});
    DrawRectangleLinesEx(boxRec, 1.0f, kPanelBorder);
    if (checked)
    {
        DrawRectangle(
            static_cast<int>(boxRec.x + 3),
            static_cast<int>(boxRec.y + 3),
            static_cast<int>(box - 6),
            static_cast<int>(box - 6),
            kPanelBorder);
    }
    DrawTextEx(
        font,
        label,
        {boxRec.x + box + 8.0f, row.y + (row.height - kFontSmall) * 0.5f},
        kFontSmall,
        1.0f,
        kTextPrimary);
}

Color ttsHighlightColor(timberline_engine::TtsHighlightKind kind)
{
    switch (kind)
    {
    case timberline_engine::TtsHighlightKind::Command:
        return Color{230, 140, 50, 255};
    case timberline_engine::TtsHighlightKind::VoiceMarkup:
        return Color{235, 210, 70, 255};
    case timberline_engine::TtsHighlightKind::VoiceDialog:
        return Color{140, 195, 235, 255};
    case timberline_engine::TtsHighlightKind::VoiceDialogError:
        return Color{180, 40, 40, 255};
    default:
        return kTextPrimary;
    }
}

Rectangle intersectRects(Rectangle a, Rectangle b)
{
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.width, b.x + b.width);
    const float y2 = std::min(a.y + a.height, b.y + b.height);
    if (x2 <= x1 || y2 <= y1)
        return {0, 0, 0, 0};
    return {x1, y1, x2 - x1, y2 - y1};
}

std::string truncateLineToWidth(
    Font font,
    const std::string& text,
    float fontSize,
    float maxWidth)
{
    if (text.empty() || MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x <= maxWidth)
        return text;
    const std::string ellipsis = "…";
    std::string out = text;
    while (!out.empty()
           && MeasureTextEx(font, (out + ellipsis).c_str(), fontSize, 1.0f).x > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

/**
 * Draw multi-line preview inside a field box. Text never paints outside the
 * box (scissor + line/height truncation with ellipsis). contentClip is the
 * outer scroll scissor to restore after drawing (raylib scissor is not nested).
 */
void drawClippedFieldPreview(
    Font font,
    Rectangle field,
    Rectangle contentClip,
    const std::string& text,
    const char* emptyHint,
    float fontSize,
    float lineSpacing)
{
    const bool empty = text.empty();
    const std::string src = empty
        ? std::string(emptyHint != nullptr ? emptyHint : "")
        : text;
    const Color color = empty ? kTextMuted : kTextPrimary;

    const float padX = 6.0f;
    const float padY = 4.0f;
    const float maxW = std::max(8.0f, field.width - padX * 2.0f);
    const float lineH = fontSize + lineSpacing;
    const int maxLines = std::max(
        1, static_cast<int>((field.height - padY * 2.0f) / lineH));

    // Word-wrap into lines (same idea as drawWrappedText).
    std::vector<std::string> lines;
    {
        std::string line;
        std::istringstream stream(src);
        std::string word;
        auto flush = [&]() {
            if (!line.empty())
            {
                lines.push_back(line);
                line.clear();
            }
        };
        while (stream >> word)
        {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (MeasureTextEx(font, candidate.c_str(), fontSize, 1.0f).x <= maxW)
            {
                line = candidate;
                continue;
            }
            flush();
            // Single word longer than maxW — hard-truncate.
            if (MeasureTextEx(font, word.c_str(), fontSize, 1.0f).x > maxW)
                line = truncateLineToWidth(font, word, fontSize, maxW);
            else
                line = word;
        }
        flush();
        // Preserve intentional blank when emptyHint is empty string.
        if (lines.empty() && !src.empty())
            lines.push_back(truncateLineToWidth(font, src, fontSize, maxW));
    }

    const bool overflow = static_cast<int>(lines.size()) > maxLines;
    if (overflow)
        lines.resize(static_cast<size_t>(maxLines));
    if (!lines.empty() && overflow)
        lines.back() = truncateLineToWidth(font, lines.back(), fontSize, maxW);

    // Clip paint to field ∩ content (nothing outside the box).
    const Rectangle inner = {
        field.x + 1.0f,
        field.y + 1.0f,
        std::max(0.0f, field.width - 2.0f),
        std::max(0.0f, field.height - 2.0f)};
    const Rectangle clip = intersectRects(inner, contentClip);
    if (clip.width <= 0.0f || clip.height <= 0.0f)
        return;

    BeginScissorMode(
        static_cast<int>(clip.x),
        static_cast<int>(clip.y),
        static_cast<int>(clip.width),
        static_cast<int>(clip.height));

    float y = field.y + padY;
    for (const std::string& line : lines)
    {
        if (y + fontSize > field.y + field.height - 1.0f)
            break;
        DrawTextEx(font, line.c_str(), {field.x + padX, y}, fontSize, 1.0f, color);
        y += lineH;
    }

    EndScissorMode();
    // Restore outer content scissor (raylib scissor is not a stack).
    BeginScissorMode(
        static_cast<int>(contentClip.x),
        static_cast<int>(contentClip.y),
        static_cast<int>(contentClip.width),
        static_cast<int>(contentClip.height));
}

} // namespace

Rectangle ItemEditor::newItemBtnBounds(Rectangle listBounds) const
{
    return {
        listBounds.x + listBounds.width - kNewItemHeaderBtnW - 8.0f,
        listBounds.y + (20.0f - kNewItemHeaderBtnH) * 0.5f,
        kNewItemHeaderBtnW,
        kNewItemHeaderBtnH};
}

Rectangle ItemEditor::editItemBtnBounds(Rectangle listBounds) const
{
    return {
        listBounds.x + listBounds.width - kNewItemHeaderBtnW - kEditItemHeaderBtnW - 14.0f,
        listBounds.y + (20.0f - kNewItemHeaderBtnH) * 0.5f,
        kEditItemHeaderBtnW,
        kNewItemHeaderBtnH};
}

std::string ItemEditor::truncateToWidth(
    Font font,
    const std::string& text,
    float fontSize,
    float maxWidth)
{
    if (text.empty())
        return text;
    if (MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x <= maxWidth)
        return text;
    std::string out = text;
    const std::string ellipsis = "…";
    while (!out.empty()
           && MeasureTextEx(font, (out + ellipsis).c_str(), fontSize, 1.0f).x > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

std::vector<std::string> ItemEditor::componentCandidateIds() const
{
    std::vector<std::string> ids;
    if (docs == nullptr)
        return ids;
    ids = docs->itemIds();
    if (!authoringPayload.id.empty())
    {
        ids.erase(
            std::remove(ids.begin(), ids.end(), authoringPayload.id),
            ids.end());
    }
    return ids;
}

void ItemEditor::drawOnOffSwitch(
    Font font,
    Rectangle track,
    bool on,
    const char* label,
    bool canClick,
    bool& outToggled)
{
    outToggled = false;
    DrawRectangleRounded(track, 0.5f, 6, Color{44, 42, 52, 255});
    DrawRectangleLinesEx(track, 1.0f, kPanelBorder);
    if (on)
    {
        DrawRectangleRec(
            {track.x + track.width * 0.5f, track.y + 1.0f,
             track.width * 0.5f - 1.0f, track.height - 2.0f},
            kPanelAccent);
    }
    else
    {
        DrawRectangleRec(
            {track.x + 1.0f, track.y + 1.0f,
             track.width * 0.5f - 1.0f, track.height - 2.0f},
            Color{36, 34, 44, 255});
    }
    const float knobSize = track.height - 6.0f;
    const float knobX = on
        ? (track.x + track.width - knobSize - 3.0f)
        : (track.x + 3.0f);
    DrawRectangleRounded(
        {knobX, track.y + 3.0f, knobSize, knobSize},
        0.5f,
        6,
        kTextPrimary);
    DrawTextEx(
        font,
        on ? "ON" : "OFF",
        {track.x + track.width + 8.0f,
         track.y + (track.height - kFontTiny) * 0.5f},
        kFontTiny,
        1.0f,
        kPanelBorder);
    if (label != nullptr && label[0] != '\0')
    {
        DrawTextEx(
            font,
            label,
            {track.x + track.width + 40.0f,
             track.y + (track.height - kFontSmall) * 0.5f},
            kFontSmall,
            1.0f,
            kTextPrimary);
    }
    if (canClick && CheckCollisionPointRec(GetMousePosition(), track))
        outToggled = true;
}

void ItemEditor::unloadAuthoringPreviews()
{
    stopAuthoringSounds();
    if (authoringPreviewExamineLoaded)
    {
        UnloadTexture(authoringPreviewExamine);
        authoringPreviewExamine = {};
        authoringPreviewExamineLoaded = false;
    }
    if (authoringPreviewIconLoaded)
    {
        UnloadTexture(authoringPreviewIcon);
        authoringPreviewIcon = {};
        authoringPreviewIconLoaded = false;
    }
    authoringPreviewExaminePath.clear();
    authoringPreviewIconPath.clear();
    if (authoringPreviewExamineSoundLoaded)
    {
        UnloadSound(authoringPreviewExamineSound);
        authoringPreviewExamineSound = {};
        authoringPreviewExamineSoundLoaded = false;
    }
    if (authoringPreviewUseSoundLoaded)
    {
        UnloadSound(authoringPreviewUseSound);
        authoringPreviewUseSound = {};
        authoringPreviewUseSoundLoaded = false;
    }
    authoringPreviewExamineSoundPath.clear();
    authoringPreviewUseSoundPath.clear();
    authoringPlayingSound = 0;
}

void ItemEditor::ensureAuthoringAudio()
{
    if (authoringAudioReady)
        return;
    InitAudioDevice();
    authoringAudioReady = IsAudioDeviceReady();
}

void ItemEditor::stopAuthoringSounds()
{
    if (authoringPreviewExamineSoundLoaded && IsSoundPlaying(authoringPreviewExamineSound))
        StopSound(authoringPreviewExamineSound);
    if (authoringPreviewUseSoundLoaded && IsSoundPlaying(authoringPreviewUseSound))
        StopSound(authoringPreviewUseSound);
    authoringPlayingSound = 0;
}

bool ItemEditor::loadAuthoringTexture(const std::string& relPath, Texture2D& outTex)
{
    if (docs == nullptr || relPath.empty())
        return false;
    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relPath);
    for (const std::string& path : paths)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str())
            && loadTextureFromAssetFile(compressed, outTex))
            return true;
        if (FileExists(path.c_str()) && loadTextureFromAssetFile(path, outTex))
            return true;
    }
    // resourceDir is often .../resources; strip leading "resources/" from rel paths.
    if (!docs->resourceDir.empty())
    {
        std::string stripped = relPath;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        const std::string direct = pathJoin(docs->resourceDir, stripped);
        const std::string compressed = compressedAssetPath(direct);
        if (FileExists(compressed.c_str())
            && loadTextureFromAssetFile(compressed, outTex))
            return true;
        if (FileExists(direct.c_str()) && loadTextureFromAssetFile(direct, outTex))
            return true;
    }
    return false;
}

bool ItemEditor::loadAuthoringSound(const std::string& relPath, Sound& outSound)
{
    if (docs == nullptr || relPath.empty())
        return false;
    ensureAuthoringAudio();
    if (!authoringAudioReady)
        return false;

    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, relPath);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = relPath;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }

    for (const std::string& path : candidates)
    {
        const std::string compressed = compressedAssetPath(path);
        std::vector<unsigned char> bytes;
        std::string fileType = ".mp3";
        const size_t dot = path.find_last_of('.');
        if (dot != std::string::npos)
            fileType = path.substr(dot);

        if (FileExists(compressed.c_str())
            && loadAssetBytesFromFile(compressed, bytes)
            && !bytes.empty())
        {
            // Strip .xz for type if needed
            std::string type = fileType;
            if (type == ".xz" && path.size() > 7)
            {
                // path may be foo.mp3.xz in compressedAssetPath form
            }
            // compressed path ends with .xz; base type from uncompressed path
            const std::string base = path;
            const size_t bdot = base.find_last_of('.');
            if (bdot != std::string::npos)
                type = base.substr(bdot);
            Wave wave = LoadWaveFromMemory(
                type.c_str(),
                bytes.data(),
                static_cast<int>(bytes.size()));
            if (wave.data != nullptr)
            {
                outSound = LoadSoundFromWave(wave);
                UnloadWave(wave);
                if (IsSoundValid(outSound))
                    return true;
            }
        }
        if (FileExists(path.c_str()))
        {
            outSound = LoadSound(path.c_str());
            if (IsSoundValid(outSound))
                return true;
        }
        if (FileExists(compressed.c_str()))
        {
            // Fallback: write temp decompressed file
            if (loadAssetBytesFromFile(compressed, bytes) && !bytes.empty())
            {
                const std::string tmp = pathJoin(
                    GetApplicationDirectory() ? GetApplicationDirectory() : ".",
                    "editor_preview_sfx.tmp" + fileType);
                std::ofstream out(tmp.c_str(), std::ios::binary);
                if (out)
                {
                    out.write(
                        reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                    out.close();
                    outSound = LoadSound(tmp.c_str());
                    std::remove(tmp.c_str());
                    if (IsSoundValid(outSound))
                        return true;
                }
            }
        }
    }
    return false;
}

void ItemEditor::syncAuthoringPreviews()
{
    const std::string& img = authoringPayload.imagePath;
    const std::string& icon = authoringPayload.iconPath;
    const std::string& exSfx = authoringPayload.examineSoundPath;
    const std::string& useSfx = authoringPayload.useSoundPath;

    if (img != authoringPreviewExaminePath)
    {
        if (authoringPreviewExamineLoaded)
        {
            UnloadTexture(authoringPreviewExamine);
            authoringPreviewExamine = {};
            authoringPreviewExamineLoaded = false;
        }
        authoringPreviewExaminePath = img;
        if (!img.empty())
            authoringPreviewExamineLoaded =
                loadAuthoringTexture(img, authoringPreviewExamine);
    }
    if (icon != authoringPreviewIconPath)
    {
        if (authoringPreviewIconLoaded)
        {
            UnloadTexture(authoringPreviewIcon);
            authoringPreviewIcon = {};
            authoringPreviewIconLoaded = false;
        }
        authoringPreviewIconPath = icon;
        if (!icon.empty())
            authoringPreviewIconLoaded =
                loadAuthoringTexture(icon, authoringPreviewIcon);
    }
    if (exSfx != authoringPreviewExamineSoundPath)
    {
        if (authoringPreviewExamineSoundLoaded)
        {
            if (IsSoundPlaying(authoringPreviewExamineSound))
                StopSound(authoringPreviewExamineSound);
            UnloadSound(authoringPreviewExamineSound);
            authoringPreviewExamineSound = {};
            authoringPreviewExamineSoundLoaded = false;
            if (authoringPlayingSound == 1)
                authoringPlayingSound = 0;
        }
        authoringPreviewExamineSoundPath = exSfx;
        if (!exSfx.empty())
            authoringPreviewExamineSoundLoaded =
                loadAuthoringSound(exSfx, authoringPreviewExamineSound);
    }
    if (useSfx != authoringPreviewUseSoundPath)
    {
        if (authoringPreviewUseSoundLoaded)
        {
            if (IsSoundPlaying(authoringPreviewUseSound))
                StopSound(authoringPreviewUseSound);
            UnloadSound(authoringPreviewUseSound);
            authoringPreviewUseSound = {};
            authoringPreviewUseSoundLoaded = false;
            if (authoringPlayingSound == 2)
                authoringPlayingSound = 0;
        }
        authoringPreviewUseSoundPath = useSfx;
        if (!useSfx.empty())
            authoringPreviewUseSoundLoaded =
                loadAuthoringSound(useSfx, authoringPreviewUseSound);
    }

    // Track play state
    if (authoringPlayingSound == 1
        && (!authoringPreviewExamineSoundLoaded
            || !IsSoundPlaying(authoringPreviewExamineSound)))
        authoringPlayingSound = 0;
    if (authoringPlayingSound == 2
        && (!authoringPreviewUseSoundLoaded
            || !IsSoundPlaying(authoringPreviewUseSound)))
        authoringPlayingSound = 0;
}

void ItemEditor::drawAuthoringPreviewPane(Font font, Rectangle pane, bool canClick)
{
    syncAuthoringPreviews();

    DrawRectangleRec(pane, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(pane, 1.0f, kPanelBorder);

    float y = pane.y + 10.0f;
    const float pad = 10.0f;
    const float innerW = pane.width - pad * 2.0f;

    DrawTextEx(font, "Preview", {pane.x + pad, y}, kFontLabel, 1.0f, kTextPrimary);
    y += 22.0f;

    // Examine image
    DrawTextEx(font, "Examine image", {pane.x + pad, y}, kFontTiny, 1.0f, kTextMuted);
    y += 16.0f;
    const float examineH = std::min(200.0f, pane.height * 0.38f);
    const Rectangle examineBox = {pane.x + pad, y, innerW, examineH};
    DrawRectangleRec(examineBox, Color{16, 14, 20, 255});
    DrawRectangleLinesEx(examineBox, 1.0f, kPanelInnerEdge);
    if (authoringPreviewExamineLoaded && authoringPreviewExamine.id != 0)
    {
        const float tw = static_cast<float>(authoringPreviewExamine.width);
        const float th = static_cast<float>(authoringPreviewExamine.height);
        const float scale = std::min(examineBox.width / tw, examineBox.height / th);
        const float dw = tw * scale;
        const float dh = th * scale;
        DrawTexturePro(
            authoringPreviewExamine,
            {0, 0, tw, th},
            {examineBox.x + (examineBox.width - dw) * 0.5f,
             examineBox.y + (examineBox.height - dh) * 0.5f,
             dw,
             dh},
            {0, 0},
            0.0f,
            WHITE);
    }
    else
    {
        const char* msg = authoringPayload.imagePath.empty()
            ? "(no image path)"
            : "(missing file)";
        DrawTextEx(
            font,
            msg,
            {examineBox.x + 8.0f, examineBox.y + examineBox.height * 0.5f - 8.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }
    y += examineH + 10.0f;

    // Icon
    DrawTextEx(font, "Icon", {pane.x + pad, y}, kFontTiny, 1.0f, kTextMuted);
    y += 16.0f;
    const float iconSize = std::min(96.0f, innerW * 0.45f);
    const Rectangle iconBox = {
        pane.x + pad + (innerW - iconSize) * 0.5f, y, iconSize, iconSize};
    DrawRectangleRec(iconBox, Color{16, 14, 20, 255});
    DrawRectangleLinesEx(iconBox, 1.0f, kPanelInnerEdge);
    if (authoringPreviewIconLoaded && authoringPreviewIcon.id != 0)
    {
        const float tw = static_cast<float>(authoringPreviewIcon.width);
        const float th = static_cast<float>(authoringPreviewIcon.height);
        const float scale = std::min(iconBox.width / tw, iconBox.height / th);
        const float dw = tw * scale;
        const float dh = th * scale;
        DrawTexturePro(
            authoringPreviewIcon,
            {0, 0, tw, th},
            {iconBox.x + (iconBox.width - dw) * 0.5f,
             iconBox.y + (iconBox.height - dh) * 0.5f,
             dw,
             dh},
            {0, 0},
            0.0f,
            WHITE);
    }
    else
    {
        const char* msg = authoringPayload.iconPath.empty() ? "(no icon)" : "(missing)";
        DrawTextEx(
            font,
            msg,
            {iconBox.x + 8.0f, iconBox.y + iconBox.height * 0.5f - 8.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }
    y += iconSize + 14.0f;

    // Sound controls
    DrawTextEx(font, "Sounds", {pane.x + pad, y}, kFontTiny, 1.0f, kTextMuted);
    y += 18.0f;

    const float btnH = 28.0f;
    const float btnW = (innerW - 8.0f) * 0.5f;
    auto drawPlayRow = [&](const char* label, int channel, bool loaded, float rowY) {
        DrawTextEx(font, label, {pane.x + pad, rowY}, kFontTiny, 1.0f, kTextMuted);
        const float by = rowY + 14.0f;
        const Rectangle playBtn = {pane.x + pad, by, btnW, btnH};
        const Rectangle stopBtn = {pane.x + pad + btnW + 8.0f, by, btnW, btnH};
        const bool playing = authoringPlayingSound == channel;
        drawEditorButton(
            font, playBtn, playing ? "Playing…" : "Play", playing, loaded);
        drawEditorButton(font, stopBtn, "Stop", false, loaded && playing);
        if (canClick && loaded)
        {
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, playBtn))
            {
                stopAuthoringSounds();
                if (channel == 1)
                {
                    PlaySound(authoringPreviewExamineSound);
                    authoringPlayingSound = 1;
                }
                else if (channel == 2)
                {
                    PlaySound(authoringPreviewUseSound);
                    authoringPlayingSound = 2;
                }
            }
            else if (CheckCollisionPointRec(mouse, stopBtn) && playing)
                stopAuthoringSounds();
        }
        return by + btnH + 10.0f;
    };

    y = drawPlayRow(
        authoringPayload.examineSoundPath.empty()
            ? "Examine sound (none)"
            : "Examine sound",
        1,
        authoringPreviewExamineSoundLoaded,
        y);
    y = drawPlayRow(
        authoringPayload.useSoundPath.empty() ? "Use sound (none)" : "Use sound",
        2,
        authoringPreviewUseSoundLoaded,
        y);

    if (!authoringAudioReady && (!authoringPayload.examineSoundPath.empty()
                                 || !authoringPayload.useSoundPath.empty()))
    {
        DrawTextEx(
            font,
            "Audio device not ready",
            {pane.x + pad, y},
            kFontTiny,
            1.0f,
            Color{220, 100, 90, 255});
    }
}

void ItemEditor::openNewItemDialog()
{
    authoringPayload = ItemAuthoringPayload{};
    authoringWeightBuffer = "0.1";
    authoringFocusField = 0;
    authoringError.clear();
    authoringIsModify = false;
    authoringDialogOpen = true;
    authoringIgnoreInputFrames = 2;
    authoringWaitMouseRelease = true;
    authoringScrollY = 0.0f;
    authoringDropdown = 0;
    authoringDropdownScroll = 0.0f;
    authoringRecipeAdvanced = false;
    unloadAuthoringPreviews();
    closeSubEdit(false);
}

void ItemEditor::openModifyItemDialog(const std::string& itemId)
{
    if (docs == nullptr || itemId.empty())
        return;
    const nlohmann::json* item = docs->itemJson(itemId);
    if (item == nullptr)
        return;
    std::string err;
    if (!loadPayloadFromItemJson(itemId, *item, authoringPayload, err))
    {
        lastAuthoringStatus = err;
        return;
    }
    char weightBuf[64];
    std::snprintf(weightBuf, sizeof(weightBuf), "%g", authoringPayload.weightLb);
    authoringWeightBuffer = weightBuf;
    authoringFocusField = 0;
    authoringError.clear();
    authoringIsModify = true;
    authoringDialogOpen = true;
    authoringIgnoreInputFrames = 2;
    authoringWaitMouseRelease = true;
    authoringScrollY = 0.0f;
    authoringDropdown = 0;
    authoringRecipeAdvanced = false;
    selectedItemId = itemId;
    selectedKey = "item-root:" + itemId;
    unloadAuthoringPreviews();
    closeSubEdit(false);
}

void ItemEditor::closeAuthoringDialog()
{
    // Wait for any in-flight generation so we don't free paths under the worker.
    if (authoringGenerateBusy.load() || authoringGenerateThread.joinable())
    {
        TraceLog(LOG_INFO, "TIMBERLINE authoring: closing dialog — joining worker");
        joinAuthoringGenerateThread();
        authoringGenerateBusy = false;
        authoringGenerateTarget = 0;
        authoringGenerateResultPending = false;
    }
    authoringDialogOpen = false;
    authoringError.clear();
    authoringIgnoreInputFrames = 0;
    authoringWaitMouseRelease = false;
    authoringDraggingScroll = false;
    authoringDropdown = 0;
    authoringRecipeAdvanced = false;
    unloadAuthoringPreviews();
    closeSubEdit(false);
}

namespace
{

/** Asset paths must live under resources/; never accept pasted API keys as paths. */
bool isPlausibleResourcePath(const std::string& path)
{
    if (path.empty())
        return false;
    if (path.rfind("resources/", 0) == 0)
        return true;
    if (path.rfind("resources\\", 0) == 0)
        return true;
    return false;
}

void authoringLog(const std::string& line)
{
    TraceLog(LOG_INFO, "TIMBERLINE authoring: %s", line.c_str());
}

} // namespace

void ItemEditor::joinAuthoringGenerateThread()
{
    if (authoringGenerateThread.joinable())
        authoringGenerateThread.join();
}

void ItemEditor::pollAuthoringGenerateResult()
{
    if (!authoringGenerateResultPending)
        return;
    std::lock_guard<std::mutex> lock(authoringGenerateMutex);
    if (!authoringGenerateResultPending)
        return;
    lastAuthoringStatus = authoringGenerateResultStatus;
    authoringGenerateResultPending = false;
    authoringGenerateBusy = false;
    authoringGenerateTarget = 0;
    // Reload previews from disk after worker finishes.
    authoringPreviewExaminePath.clear();
    authoringPreviewIconPath.clear();
    authoringPreviewExamineSoundPath.clear();
    authoringPreviewUseSoundPath.clear();
    syncAuthoringPreviews();
    joinAuthoringGenerateThread();
    authoringLog("UI applied generate result: " + lastAuthoringStatus);
}

bool ItemEditor::generateAuthoringAssetsNow(int target)
{
    if (authoringGenerateBusy.load())
    {
        lastAuthoringStatus = "Generation already in progress…";
        return false;
    }
    if (docs == nullptr)
    {
        lastAuthoringStatus = "No document workspace.";
        return false;
    }
    if (authoringPayload.name.empty() || authoringPayload.description.empty())
    {
        lastAuthoringStatus =
            "Enter a name and description before generating assets.";
        return false;
    }
    if (authoringPayload.id.empty())
        authoringPayload.id = slugifyItemId(authoringPayload.name);
    if (authoringPayload.id.empty())
    {
        lastAuthoringStatus = "Could not derive an item id from the name.";
        return false;
    }

    // Sanitize paths — never treat an API key (or other garbage) as an out path.
    auto ensurePath = [](std::string& path, const std::string& fallback) {
        if (!isPlausibleResourcePath(path))
            path = fallback;
    };
    ensurePath(
        authoringPayload.imagePath,
        defaultItemExamineImagePath(authoringPayload.id));
    ensurePath(
        authoringPayload.iconPath, defaultItemIconPath(authoringPayload.id));
    ensurePath(
        authoringPayload.examineSoundPath,
        defaultItemExamineSoundPath(authoringPayload.id));
    ensurePath(
        authoringPayload.useSoundPath,
        defaultItemUseSoundPath(authoringPayload.id));

    // Configure which jobs to run based on target.
    ItemAiAssistSwitches flags{};
    if (target == 1)
        flags.generateImageFromDescription = true;
    else if (target == 2)
        flags.generateIconFromDescription = true;
    else if (target == 3)
        flags.generateExamineSound = true;
    else if (target == 4)
        flags.generateUseSound = true;
    else if (target == 5)
    {
        flags.generateImageFromDescription = true;
        flags.generateIconFromDescription = true;
        flags.generateExamineSound = true;
        flags.generateUseSound = true;
    }
    else
    {
        // 0 = whatever is already flagged on the payload.
        flags = authoringPayload.aiAssist;
        flags.assistConstructionDescription = false;
        flags.assistTtsConstructionDescription = false;
        flags.assistTtsDescription = false;
    }

    // Mirror flags onto payload so UI paths stay consistent after run.
    if (flags.generateImageFromDescription)
        authoringPayload.aiAssist.generateImageFromDescription = true;
    if (flags.generateIconFromDescription)
        authoringPayload.aiAssist.generateIconFromDescription = true;
    if (flags.generateExamineSound)
        authoringPayload.aiAssist.generateExamineSound = true;
    if (flags.generateUseSound)
        authoringPayload.aiAssist.generateUseSound = true;

    const bool needsImage =
        flags.generateImageFromDescription || flags.generateIconFromDescription;
    if (needsImage && authoringAiApiKey.empty())
    {
        lastAuthoringStatus =
            "Paste an xAI API key in the AI Assist section (session only), "
            "then press Generate.";
        authoringLog("Generate blocked: missing session API key for image jobs");
        return false;
    }

    ItemAuthoringPayload planPayload = authoringPayload;
    planPayload.aiAssist = flags;
    const ItemAiAssistPlan plan = planItemAiAssist(planPayload);
    if (plan.empty())
    {
        lastAuthoringStatus =
            "Nothing to generate. Use a row Generate button or Generate all assets.";
        return false;
    }

    std::string jobsError;
    if (!writeItemAiAssistJobsFile(
            docs->assetRoot, authoringPayload.id, plan, jobsError))
    {
        lastAuthoringStatus = "Failed to write jobs file: " + jobsError;
        authoringLog(lastAuthoringStatus);
        return false;
    }

    // Snapshot for the worker thread (payload/docs pointers must stay valid —
    // dialog stays open while busy; closeAuthoringDialog joins the thread).
    const std::string assetRoot = docs->assetRoot;
    const std::string itemId = authoringPayload.id;
    const std::string apiKey = authoringAiApiKey;
    const int targetSnap = target == 0 ? 5 : target;

    authoringLog(
        "Starting generate target=" + std::to_string(targetSnap)
        + " item=" + itemId + " jobs=" + std::to_string(plan.jobs.size())
        + " hasKey=" + std::string(apiKey.empty() ? "no" : "yes"));
    for (const auto& job : plan.jobs)
        authoringLog("  job outPath=" + job.outPath);

    joinAuthoringGenerateThread();
    authoringGenerateBusy = true;
    authoringGenerateTarget = targetSnap;
    authoringGenerateResultPending = false;
    lastAuthoringStatus = "Working… generating assets (see console / .authoring log)";

    authoringGenerateThread = std::thread(
        [this, assetRoot, itemId, apiKey]() {
            authoringLog("Worker thread started for " + itemId);
            std::string runStatus;
            const bool ok =
                runItemAuthoringAiJobs(assetRoot, itemId, runStatus, apiKey);
            authoringLog(
                std::string("Worker finished ok=") + (ok ? "true" : "false")
                + " status=" + runStatus);
            std::lock_guard<std::mutex> lock(authoringGenerateMutex);
            authoringGenerateResultStatus = ok
                ? ("Generated assets for " + itemId + " — " + runStatus)
                : ("[AI assets FAILED] " + runStatus);
            authoringGenerateResultPending = true;
            // busy flag cleared on main thread in pollAuthoringGenerateResult
        });

    return true;
}

bool ItemEditor::commitAuthoringDialog()
{
    if (docs == nullptr)
    {
        authoringError = "No document workspace.";
        return false;
    }
    float weight = 0.0f;
    if (std::sscanf(authoringWeightBuffer.c_str(), "%f", &weight) != 1)
    {
        authoringError = "Weight must be a number (lb).";
        return false;
    }
    authoringPayload.weightLb = weight;
    if (!authoringIsModify && authoringPayload.id.empty())
        authoringPayload.id = slugifyItemId(authoringPayload.name);

    // If advanced recipe panel was never opened, don't force advanced JSON.
    if (!authoringRecipeAdvanced)
        authoringPayload.recipe.advancedComponentsJson.clear();

    const ItemAuthoringResult result =
        upsertItemFromPayload(*docs, authoringPayload, !authoringIsModify);
    if (!result.ok)
    {
        authoringError = result.error.empty() ? "Failed to save item." : result.error;
        return false;
    }
    if (!docs->saveItemsDocument())
        authoringError = "Saved in memory but write to items.json failed.";

    lastAuthoringStatus = (authoringIsModify ? "Updated " : "Created ") + result.itemId;
    if (!result.aiStatus.empty())
        lastAuthoringStatus += " — " + result.aiStatus;

    // Run image/SFX generation for any AI assist jobs (paths alone are not enough).
    // Also re-run if a jobs file already lists image/sound types (Modify re-create).
    const bool wantsAssetJobs =
        authoringPayload.aiAssist.generateImageFromDescription
        || authoringPayload.aiAssist.generateIconFromDescription
        || authoringPayload.aiAssist.generateExamineSound
        || authoringPayload.aiAssist.generateUseSound
        || !result.aiPlan.empty();
    if (wantsAssetJobs && !result.aiPlan.empty())
    {
        std::string runStatus;
        const bool ok = runItemAuthoringAiJobs(
            docs->assetRoot, result.itemId, runStatus, authoringAiApiKey);
        lastAuthoringStatus += ok ? ("\n" + runStatus) : ("\n[AI assets FAILED] " + runStatus);
        // Keep the dialog closed but leave a sticky error if images were requested
        // and the runner could not produce them (usually missing API key).
        if (!ok
            && (authoringPayload.aiAssist.generateImageFromDescription
                || authoringPayload.aiAssist.generateIconFromDescription))
        {
            authoringError =
                "Item saved, but image generation failed. "
                "Paste your xAI API key in AI Assist (session only), then "
                "Edit Item → Save again. "
                + runStatus;
            // Surface via lastAuthoringStatus on the main pane.
        }
        // Paths may have been normalized to .mp3 by the runner; reload payload paths if present.
        if (ok)
        {
            if (authoringPayload.aiAssist.generateExamineSound
                && authoringPayload.examineSoundPath.find(".opus") != std::string::npos)
            {
                authoringPayload.examineSoundPath =
                    defaultItemExamineSoundPath(result.itemId);
            }
            if (authoringPayload.aiAssist.generateUseSound
                && authoringPayload.useSoundPath.find(".opus") != std::string::npos)
            {
                authoringPayload.useSoundPath = defaultItemUseSoundPath(result.itemId);
            }
            // Re-apply paths after generation in case defaults were fixed.
            nlohmann::json* item = docs->itemJson(result.itemId);
            if (item != nullptr)
            {
                if (!authoringPayload.imagePath.empty())
                    (*item)["visuals"]["image"] = authoringPayload.imagePath;
                if (!authoringPayload.iconPath.empty())
                    (*item)["icons"]["icon"] = authoringPayload.iconPath;
                if (!authoringPayload.examineSoundPath.empty()
                    || !authoringPayload.useSoundPath.empty())
                {
                    if (!item->contains("sfx") || !(*item)["sfx"].is_object())
                        (*item)["sfx"] = nlohmann::json::object();
                    if (!authoringPayload.examineSoundPath.empty())
                        (*item)["sfx"]["examine"] = authoringPayload.examineSoundPath;
                    if (!authoringPayload.useSoundPath.empty())
                        (*item)["sfx"]["use"] = authoringPayload.useSoundPath;
                }
                docs->markDirty();
                docs->saveItemsDocument();
            }
        }
    }

    selectedItemId = result.itemId;
    selectedKey = "item-root:" + result.itemId;
    rebuildTree();
    expanded.insert(selectedKey);
    closeAuthoringDialog();
    return true;
}

// ---------- Sub-edit popup (description / TTS / sounds / paths) ----------

void ItemEditor::openSubEdit(SubEditKind kind)
{
    subEditKind = kind;
    subEditScrollY = 0.0f;
    subEditIgnoreFrames = 1;
    subEditSelectAnchor = -1;
    subEditPreferX = 0.0f;
    subEditMouseSelecting = false;
    subEditSyntaxHighlight = false;
    switch (kind)
    {
    case SubEditKind::Description:
        subEditTitle = "Description";
        subEditBuffer = authoringPayload.description;
        break;
    case SubEditKind::TtsDescription:
        subEditTitle = "TTS Description";
        subEditBuffer = authoringPayload.ttsDescription;
        subEditSyntaxHighlight = true;
        break;
    case SubEditKind::ConstructionDescription:
        subEditTitle = "Construction Description";
        subEditBuffer = authoringPayload.recipe.constructionDescription;
        break;
    case SubEditKind::TtsConstructionDescription:
        subEditTitle = "TTS Construction Description";
        subEditBuffer = authoringPayload.recipe.ttsConstructionDescription;
        subEditSyntaxHighlight = true;
        break;
    case SubEditKind::ImagePath:
        subEditTitle = "Examine Image Path";
        subEditBuffer = authoringPayload.imagePath;
        break;
    case SubEditKind::IconPath:
        subEditTitle = "Icon Path";
        subEditBuffer = authoringPayload.iconPath;
        break;
    case SubEditKind::ExamineSound:
        subEditTitle = "Examine Sound";
        subEditBuffer = authoringPayload.examineSoundPath;
        break;
    case SubEditKind::UseSound:
        subEditTitle = "Use Sound";
        subEditBuffer = authoringPayload.useSoundPath;
        break;
    case SubEditKind::RecipeAdvancedJson:
        subEditTitle = "Product Recipe Components (JSON)";
        if (authoringPayload.recipe.advancedComponentsJson.empty()
            && authoringPayload.recipe.enabled)
        {
            nlohmann::json arr = nlohmann::json::array();
            if (!authoringPayload.recipe.component1.empty())
            {
                arr.push_back({
                    {"itemId", authoringPayload.recipe.component1},
                    {"reqQty", 1},
                    {"consume", true}});
            }
            if (!authoringPayload.recipe.component2.empty())
            {
                arr.push_back({
                    {"itemId", authoringPayload.recipe.component2},
                    {"reqQty", 1},
                    {"consume", true}});
            }
            authoringPayload.recipe.advancedComponentsJson = arr.dump(2);
        }
        subEditBuffer = authoringPayload.recipe.advancedComponentsJson;
        break;
    default:
        return;
    }
    subEditCursor = static_cast<int>(subEditBuffer.size());
    subEditOpen = true;
}

void ItemEditor::closeSubEdit(bool apply)
{
    if (apply && subEditOpen)
    {
        switch (subEditKind)
        {
        case SubEditKind::Description:
            authoringPayload.description = subEditBuffer;
            break;
        case SubEditKind::TtsDescription:
            authoringPayload.ttsDescription = subEditBuffer;
            break;
        case SubEditKind::ConstructionDescription:
            authoringPayload.recipe.constructionDescription = subEditBuffer;
            break;
        case SubEditKind::TtsConstructionDescription:
            authoringPayload.recipe.ttsConstructionDescription = subEditBuffer;
            break;
        case SubEditKind::ImagePath:
            authoringPayload.imagePath = subEditBuffer;
            break;
        case SubEditKind::IconPath:
            authoringPayload.iconPath = subEditBuffer;
            break;
        case SubEditKind::ExamineSound:
            authoringPayload.examineSoundPath = subEditBuffer;
            break;
        case SubEditKind::UseSound:
            authoringPayload.useSoundPath = subEditBuffer;
            break;
        case SubEditKind::RecipeAdvancedJson:
            authoringPayload.recipe.advancedComponentsJson = subEditBuffer;
            authoringRecipeAdvanced = true;
            break;
        default:
            break;
        }
    }
    subEditOpen = false;
    subEditKind = SubEditKind::None;
    subEditBuffer.clear();
    subEditTitle.clear();
    subEditCursor = 0;
    subEditSelectAnchor = -1;
    subEditScrollY = 0.0f;
    subEditIgnoreFrames = 0;
    subEditSyntaxHighlight = false;
}

bool ItemEditor::subEditHasSelection() const
{
    return subEditSelectAnchor >= 0 && subEditSelectAnchor != subEditCursor;
}

void ItemEditor::subEditSelectionRange(int& start, int& end) const
{
    start = std::min(subEditCursor, subEditSelectAnchor);
    end = std::max(subEditCursor, subEditSelectAnchor);
    if (start < 0)
        start = 0;
    if (end > static_cast<int>(subEditBuffer.size()))
        end = static_cast<int>(subEditBuffer.size());
}

void ItemEditor::subEditDeleteSelection()
{
    if (!subEditHasSelection())
        return;
    int start = 0;
    int end = 0;
    subEditSelectionRange(start, end);
    subEditBuffer.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
    subEditCursor = start;
    subEditSelectAnchor = -1;
}

void ItemEditor::subEditSetCursor(int pos, bool extend)
{
    if (pos < 0)
        pos = 0;
    if (pos > static_cast<int>(subEditBuffer.size()))
        pos = static_cast<int>(subEditBuffer.size());
    if (extend)
    {
        if (subEditSelectAnchor < 0)
            subEditSelectAnchor = subEditCursor;
    }
    else
        subEditSelectAnchor = -1;
    subEditCursor = pos;
}

bool ItemEditor::subEditNavKeyTriggered(int key)
{
    if (IsKeyPressed(key))
    {
        subEditKeyRepeatKey = key;
        subEditKeyRepeatTimer = kKeyRepeatInitialDelaySeconds;
        return true;
    }
    if (IsKeyDown(key) && subEditKeyRepeatKey == key)
    {
        subEditKeyRepeatTimer -= GetFrameTime();
        if (subEditKeyRepeatTimer <= 0.0f)
        {
            subEditKeyRepeatTimer = kKeyRepeatEverySeconds;
            return true;
        }
    }
    else if (subEditKeyRepeatKey == key && !IsKeyDown(key))
        subEditKeyRepeatKey = 0;
    return false;
}

std::vector<EditorVisualLine> ItemEditor::subEditBuildLines(
    float maxWidth,
    float fontSize) const
{
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    std::vector<EditorVisualLine> lines;
    const std::string& text = subEditBuffer;
    if (text.empty())
    {
        EditorVisualLine empty;
        empty.start = 0;
        empty.end = 0;
        lines.push_back(empty);
        return lines;
    }

    int lineStart = 0;
    while (lineStart <= static_cast<int>(text.size()))
    {
        // Find hard newline or end
        int hardEnd = lineStart;
        while (hardEnd < static_cast<int>(text.size()) && text[static_cast<size_t>(hardEnd)] != '\n')
            ++hardEnd;

        // Soft-wrap within [lineStart, hardEnd)
        int segStart = lineStart;
        while (segStart < hardEnd)
        {
            int segEnd = segStart;
            int lastBreak = -1;
            while (segEnd < hardEnd)
            {
                const int next = segEnd + 1;
                const std::string slice =
                    text.substr(static_cast<size_t>(segStart), static_cast<size_t>(next - segStart));
                if (MeasureTextEx(font, slice.c_str(), fontSize, 1.0f).x > maxWidth
                    && segEnd > segStart)
                    break;
                if (text[static_cast<size_t>(segEnd)] == ' ')
                    lastBreak = segEnd + 1;
                segEnd = next;
            }
            if (segEnd < hardEnd && lastBreak > segStart)
                segEnd = lastBreak;
            if (segEnd == segStart)
                segEnd = std::min(hardEnd, segStart + 1);

            EditorVisualLine line;
            line.start = segStart;
            line.end = segEnd;
            line.text = text.substr(
                static_cast<size_t>(segStart),
                static_cast<size_t>(segEnd - segStart));
            // Strip trailing newline from display text if any
            if (!line.text.empty() && line.text.back() == '\n')
                line.text.pop_back();
            lines.push_back(line);
            segStart = segEnd;
        }

        if (hardEnd < static_cast<int>(text.size()) && text[static_cast<size_t>(hardEnd)] == '\n')
        {
            // Empty line after trailing newline when segment was empty
            if (segStart == lineStart)
            {
                EditorVisualLine line;
                line.start = hardEnd;
                line.end = hardEnd + 1;
                line.text.clear();
                lines.push_back(line);
            }
            lineStart = hardEnd + 1;
            if (lineStart == static_cast<int>(text.size()))
            {
                // Trailing newline → blank last line
                EditorVisualLine line;
                line.start = lineStart;
                line.end = lineStart;
                lines.push_back(line);
            }
        }
        else
            break;
    }
    if (lines.empty())
    {
        EditorVisualLine empty;
        empty.start = 0;
        empty.end = 0;
        lines.push_back(empty);
    }
    return lines;
}

namespace
{

/** Same metric VariableEditor uses: MeasureTextEx with spacing 1.0. */
float subEditMeasureWidth(Font font, const std::string& text, float fontSize)
{
    if (text.empty())
        return 0.0f;
    return MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x;
}

/**
 * Visible slice of a visual line (excludes a trailing hard newline that may
 * be counted in [start, end) for caret bookkeeping).
 */
std::string subEditLineDisplayText(const std::string& buffer, const EditorVisualLine& line)
{
    if (line.start >= static_cast<int>(buffer.size()) || line.start >= line.end)
        return {};
    int end = line.end;
    if (end > static_cast<int>(buffer.size()))
        end = static_cast<int>(buffer.size());
    // Never draw a hard newline glyph.
    if (end > line.start && buffer[static_cast<size_t>(end - 1)] == '\n')
        --end;
    if (end <= line.start)
        return {};
    return buffer.substr(static_cast<size_t>(line.start), static_cast<size_t>(end - line.start));
}

float subEditCaretXOnLine(
    Font font,
    const std::string& buffer,
    const EditorVisualLine& line,
    int cursor,
    float fontSize)
{
    int at = cursor;
    if (at < line.start)
        at = line.start;
    // Caret may sit on the exclusive end index (end of line).
    int lineVisEnd = line.end;
    if (lineVisEnd > static_cast<int>(buffer.size()))
        lineVisEnd = static_cast<int>(buffer.size());
    if (lineVisEnd > line.start
        && lineVisEnd <= static_cast<int>(buffer.size())
        && buffer[static_cast<size_t>(lineVisEnd - 1)] == '\n')
        --lineVisEnd;
    if (at > lineVisEnd)
        at = lineVisEnd;
    if (at <= line.start)
        return 0.0f;
    const std::string before = buffer.substr(
        static_cast<size_t>(line.start),
        static_cast<size_t>(at - line.start));
    return subEditMeasureWidth(font, before, fontSize);
}

} // namespace

void ItemEditor::subEditDrawHighlightedText(
    Font font,
    const std::vector<EditorVisualLine>& lines,
    Rectangle field,
    float fontSize,
    float lineHeight) const
{
    std::vector<timberline_engine::TtsHighlightKind> kinds;
    if (subEditSyntaxHighlight)
        timberline_engine::classifyTtsTextHighlight(subEditBuffer, kinds);

    int selStart = 0;
    int selEnd = 0;
    const bool hasSel = subEditHasSelection();
    if (hasSel)
        subEditSelectionRange(selStart, selEnd);

    float y = field.y + kSubEditPad - subEditScrollY;
    for (const EditorVisualLine& line : lines)
    {
        if (y + lineHeight < field.y || y > field.y + field.height)
        {
            y += lineHeight;
            continue;
        }

        const std::string display = subEditLineDisplayText(subEditBuffer, line);
        const float baseX = field.x + kSubEditPad;

        // Selection background — same measure path as caret/text.
        if (hasSel)
        {
            const int visEnd = line.start + static_cast<int>(display.size());
            const int ls = std::max(line.start, selStart);
            const int le = std::min(visEnd, selEnd);
            if (ls < le)
            {
                const float x0 = baseX
                    + subEditCaretXOnLine(font, subEditBuffer, line, ls, fontSize);
                const float x1 = baseX
                    + subEditCaretXOnLine(font, subEditBuffer, line, le, fontSize);
                DrawRectangle(
                    static_cast<int>(x0),
                    static_cast<int>(y),
                    static_cast<int>(std::max(2.0f, x1 - x0)),
                    static_cast<int>(lineHeight),
                    Color{80, 70, 40, 180});
            }
        }

        // Draw like VariableEditor: whole line or same-color runs (never per-glyph).
        if (display.empty())
        {
            y += lineHeight;
            continue;
        }
        if (!subEditSyntaxHighlight)
        {
            DrawTextEx(font, display.c_str(), {baseX, y}, fontSize, 1.0f, kTextPrimary);
        }
        else
        {
            float drawX = baseX;
            size_t i = 0;
            while (i < display.size())
            {
                const int bufIdx = line.start + static_cast<int>(i);
                const Color runColor =
                    (bufIdx >= 0 && bufIdx < static_cast<int>(kinds.size()))
                    ? ttsHighlightColor(kinds[static_cast<size_t>(bufIdx)])
                    : kTextPrimary;
                size_t j = i + 1;
                while (j < display.size())
                {
                    const int jIdx = line.start + static_cast<int>(j);
                    const Color c =
                        (jIdx >= 0 && jIdx < static_cast<int>(kinds.size()))
                        ? ttsHighlightColor(kinds[static_cast<size_t>(jIdx)])
                        : kTextPrimary;
                    if (c.r != runColor.r || c.g != runColor.g
                        || c.b != runColor.b || c.a != runColor.a)
                        break;
                    ++j;
                }
                const std::string run = display.substr(i, j - i);
                DrawTextEx(font, run.c_str(), {drawX, y}, fontSize, 1.0f, runColor);
                drawX += subEditMeasureWidth(font, run, fontSize);
                i = j;
            }
        }
        y += lineHeight;
    }
}

void ItemEditor::handleSubEditInput()
{
    if (!subEditOpen)
        return;
    if (subEditIgnoreFrames > 0)
    {
        --subEditIgnoreFrames;
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        closeSubEdit(false);
        return;
    }
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
         || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        && IsKeyPressed(KEY_ENTER))
    {
        closeSubEdit(true);
        return;
    }

    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (mod && IsKeyPressed(KEY_A))
    {
        subEditSelectAnchor = 0;
        subEditCursor = static_cast<int>(subEditBuffer.size());
        return;
    }
    if (mod && IsKeyPressed(KEY_C) && subEditHasSelection())
    {
        int s = 0;
        int e = 0;
        subEditSelectionRange(s, e);
        SetClipboardText(
            subEditBuffer.substr(static_cast<size_t>(s), static_cast<size_t>(e - s)).c_str());
        return;
    }
    if (mod && IsKeyPressed(KEY_X) && subEditHasSelection())
    {
        int s = 0;
        int e = 0;
        subEditSelectionRange(s, e);
        SetClipboardText(
            subEditBuffer.substr(static_cast<size_t>(s), static_cast<size_t>(e - s)).c_str());
        subEditDeleteSelection();
        return;
    }
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            if (subEditHasSelection())
                subEditDeleteSelection();
            subEditBuffer.insert(static_cast<size_t>(subEditCursor), clip);
            subEditCursor += static_cast<int>(std::strlen(clip));
            subEditSelectAnchor = -1;
        }
        return;
    }

    if (subEditNavKeyTriggered(KEY_LEFT))
        subEditSetCursor(subEditCursor - 1, shift);
    if (subEditNavKeyTriggered(KEY_RIGHT))
        subEditSetCursor(subEditCursor + 1, shift);
    if (subEditNavKeyTriggered(KEY_HOME))
        subEditSetCursor(0, shift);
    if (subEditNavKeyTriggered(KEY_END))
        subEditSetCursor(static_cast<int>(subEditBuffer.size()), shift);

    {
        int move = 0;
        if (subEditNavKeyTriggered(KEY_UP))
            move = -1;
        else if (subEditNavKeyTriggered(KEY_DOWN))
            move = 1;
        if (move != 0)
        {
            const float maxW = subEditFieldRect.width - kSubEditPad * 2.0f;
            const std::vector<EditorVisualLine> lines =
                subEditBuildLines(std::max(40.0f, maxW), kSubEditFont);
            int lineIdx = 0;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (subEditCursor >= lines[i].start && subEditCursor <= lines[i].end)
                    lineIdx = static_cast<int>(i);
            }
            const int target = lineIdx + move;
            if (target >= 0 && target < static_cast<int>(lines.size()))
            {
                const EditorVisualLine& line = lines[static_cast<size_t>(target)];
                const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
                int best = line.start;
                float bestDx = 1e9f;
                for (int i = line.start; i <= line.end; ++i)
                {
                    const std::string prefix = subEditBuffer.substr(
                        static_cast<size_t>(line.start),
                        static_cast<size_t>(i - line.start));
                    const float x = MeasureTextEx(font, prefix.c_str(), kSubEditFont, 1.0f).x;
                    const float dx = std::fabs(x - subEditPreferX);
                    if (dx < bestDx)
                    {
                        bestDx = dx;
                        best = i;
                    }
                }
                subEditSetCursor(best, shift);
            }
        }
    }

    if (subEditNavKeyTriggered(KEY_BACKSPACE))
    {
        if (subEditHasSelection())
            subEditDeleteSelection();
        else if (subEditCursor > 0)
        {
            subEditBuffer.erase(static_cast<size_t>(subEditCursor - 1), 1);
            --subEditCursor;
        }
    }
    if (subEditNavKeyTriggered(KEY_DELETE))
    {
        if (subEditHasSelection())
            subEditDeleteSelection();
        else if (subEditCursor < static_cast<int>(subEditBuffer.size()))
            subEditBuffer.erase(static_cast<size_t>(subEditCursor), 1);
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        if (!(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
              || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)))
        {
            if (subEditHasSelection())
                subEditDeleteSelection();
            subEditBuffer.insert(static_cast<size_t>(subEditCursor), "\n");
            ++subEditCursor;
            subEditSelectAnchor = -1;
        }
    }

    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        if (codepoint >= 32)
        {
            if (subEditHasSelection())
                subEditDeleteSelection();
            std::string ch;
            appendUtf8Codepoint(ch, codepoint);
            subEditBuffer.insert(static_cast<size_t>(subEditCursor), ch);
            subEditCursor += static_cast<int>(ch.size());
            subEditSelectAnchor = -1;
        }
        codepoint = GetCharPressed();
    }

    // Mouse selection / click-to-place caret
    const Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(mouse, subEditFieldRect))
    {
        const float maxW = subEditFieldRect.width - kSubEditPad * 2.0f;
        const std::vector<EditorVisualLine> lines =
            subEditBuildLines(std::max(40.0f, maxW), kSubEditFont);
        const float localY =
            (mouse.y - subEditFieldRect.y - kSubEditPad) + subEditScrollY;
        int lineIdx = static_cast<int>(localY / kSubEditLineH);
        if (lineIdx < 0)
            lineIdx = 0;
        if (lineIdx >= static_cast<int>(lines.size()))
            lineIdx = static_cast<int>(lines.size()) - 1;
        const EditorVisualLine& line = lines[static_cast<size_t>(lineIdx)];
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const float localX = mouse.x - subEditFieldRect.x - kSubEditPad;
        subEditPreferX = localX;
        const int visEnd = line.start
            + static_cast<int>(subEditLineDisplayText(subEditBuffer, line).size());
        int best = line.start;
        float bestDx = 1e9f;
        for (int i = line.start; i <= visEnd; ++i)
        {
            const float x = subEditCaretXOnLine(font, subEditBuffer, line, i, kSubEditFont);
            const float dx = std::fabs(x - localX);
            if (dx < bestDx)
            {
                bestDx = dx;
                best = i;
            }
        }
        subEditSetCursor(best, shift);
        subEditMouseSelecting = true;
    }
    if (subEditMouseSelecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        if (subEditSelectAnchor < 0)
            subEditSelectAnchor = subEditCursor;
        const float maxW = subEditFieldRect.width - kSubEditPad * 2.0f;
        const std::vector<EditorVisualLine> lines =
            subEditBuildLines(std::max(40.0f, maxW), kSubEditFont);
        const float localY =
            (mouse.y - subEditFieldRect.y - kSubEditPad) + subEditScrollY;
        int lineIdx = static_cast<int>(localY / kSubEditLineH);
        if (lineIdx < 0)
            lineIdx = 0;
        if (lineIdx >= static_cast<int>(lines.size()))
            lineIdx = static_cast<int>(lines.size()) - 1;
        const EditorVisualLine& line = lines[static_cast<size_t>(lineIdx)];
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const float localX = mouse.x - subEditFieldRect.x - kSubEditPad;
        const int visEnd = line.start
            + static_cast<int>(subEditLineDisplayText(subEditBuffer, line).size());
        int best = line.start;
        float bestDx = 1e9f;
        for (int i = line.start; i <= visEnd; ++i)
        {
            const float x = subEditCaretXOnLine(font, subEditBuffer, line, i, kSubEditFont);
            const float dx = std::fabs(x - localX);
            if (dx < bestDx)
            {
                bestDx = dx;
                best = i;
            }
        }
        subEditCursor = best;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        subEditMouseSelecting = false;

    if (CheckCollisionPointRec(mouse, subEditFieldRect))
    {
        subEditScrollY -= GetMouseWheelMove() * kSubEditLineH * 2.0f;
        if (subEditScrollY < 0.0f)
            subEditScrollY = 0.0f;
    }
}

void ItemEditor::drawSubEditDialog(int screenWidth, int screenHeight)
{
    if (!subEditOpen)
        return;
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    DrawRectangle(0, 0, screenWidth, screenHeight, kModalOverlay);

    const float dialogW = std::min(740.0f, static_cast<float>(screenWidth) - 40.0f);
    const float dialogH = std::min(460.0f, static_cast<float>(screenHeight) - 40.0f);
    const Rectangle dialog = {
        (static_cast<float>(screenWidth) - dialogW) * 0.5f,
        (static_cast<float>(screenHeight) - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRounded(dialog, 0.03f, 8, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        font,
        subEditTitle.c_str(),
        {dialog.x + 18.0f, dialog.y + 14.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        subEditSyntaxHighlight
            ? "TTS syntax highlighting  ·  Ctrl/Cmd+Enter apply  ·  Esc cancel"
            : "Arrows / select / copy-paste  ·  Ctrl/Cmd+Enter apply  ·  Esc cancel",
        {dialog.x + 18.0f, dialog.y + 40.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float btnH = 34.0f;
    const float btnW = 110.0f;
    const float btnY = dialog.y + dialogH - btnH - 16.0f;
    subEditFieldRect = {
        dialog.x + 18.0f,
        dialog.y + 62.0f,
        dialogW - 36.0f,
        btnY - (dialog.y + 62.0f) - 12.0f};
    DrawRectangleRec(subEditFieldRect, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(subEditFieldRect, 1.0f, kPanelBorder);

    const float maxW = subEditFieldRect.width - kSubEditPad * 2.0f;
    const std::vector<EditorVisualLine> lines =
        subEditBuildLines(std::max(40.0f, maxW), kSubEditFont);
    const float contentH = static_cast<float>(lines.size()) * kSubEditLineH + kSubEditPad * 2.0f;
    const float maxScroll = std::max(0.0f, contentH - subEditFieldRect.height);
    // Line for caret: prefer the line whose [start, nextStart) contains cursor
    // (matches VariableEditor — end-of-line wrap lands on the next visual line).
    int caretLine = 0;
    if (!lines.empty())
    {
        caretLine = static_cast<int>(lines.size()) - 1;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const int nextStart = (i + 1 < lines.size())
                ? lines[i + 1].start
                : (static_cast<int>(subEditBuffer.size()) + 1);
            if (subEditCursor >= lines[i].start && subEditCursor < nextStart)
            {
                caretLine = static_cast<int>(i);
                break;
            }
        }
    }
    const float caretY = static_cast<float>(caretLine) * kSubEditLineH;
    if (caretY < subEditScrollY)
        subEditScrollY = caretY;
    if (caretY + kSubEditLineH > subEditScrollY + subEditFieldRect.height - kSubEditPad * 2.0f)
        subEditScrollY = caretY + kSubEditLineH - (subEditFieldRect.height - kSubEditPad * 2.0f);
    if (subEditScrollY > maxScroll)
        subEditScrollY = maxScroll;
    if (subEditScrollY < 0.0f)
        subEditScrollY = 0.0f;

    BeginScissorMode(
        static_cast<int>(subEditFieldRect.x + 1),
        static_cast<int>(subEditFieldRect.y + 1),
        static_cast<int>(subEditFieldRect.width - 2),
        static_cast<int>(subEditFieldRect.height - 2));
    subEditDrawHighlightedText(
        font, lines, subEditFieldRect, kSubEditFont, kSubEditLineH);

    // Caret: same width metric as DrawTextEx runs (no per-glyph sum, no trailing '|').
    if (!lines.empty())
    {
        const EditorVisualLine& line = lines[static_cast<size_t>(caretLine)];
        const float caretX = subEditCaretXOnLine(
            font, subEditBuffer, line, subEditCursor, kSubEditFont);
        const float cx = subEditFieldRect.x + kSubEditPad + caretX;
        const float cy = subEditFieldRect.y + kSubEditPad - subEditScrollY
            + static_cast<float>(caretLine) * kSubEditLineH;
        if (static_cast<int>(GetTime() * 2.0) % 2 == 0)
        {
            DrawRectangle(
                static_cast<int>(cx),
                static_cast<int>(cy),
                2,
                static_cast<int>(kSubEditLineH - 2),
                kTextPrimary);
        }
        subEditPreferX = caretX;
    }
    EndScissorMode();

    const Rectangle saveBtn = {dialog.x + dialogW - btnW * 2.0f - 32.0f, btnY, btnW, btnH};
    const Rectangle cancelBtn = {dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};
    drawEditorButton(font, saveBtn, "Apply", true, true);
    drawEditorButton(font, cancelBtn, "Cancel", false, true);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !subEditMouseSelecting)
    {
        const Vector2 mouse = GetMousePosition();
        if (CheckCollisionPointRec(mouse, saveBtn))
            closeSubEdit(true);
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeSubEdit(false);
        else if (!CheckCollisionPointRec(mouse, dialog)
                 && !CheckCollisionPointRec(mouse, subEditFieldRect))
            closeSubEdit(false);
    }
}

void ItemEditor::handleAuthoringDialogInput(int screenWidth, int screenHeight)
{
    if (!authoringDialogOpen)
        return;
    (void)screenWidth;
    (void)screenHeight;

    if (subEditOpen)
    {
        handleSubEditInput();
        return;
    }

    if (authoringIgnoreInputFrames > 0)
    {
        --authoringIgnoreInputFrames;
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (authoringDropdown != 0)
            authoringDropdown = 0;
        else
            closeAuthoringDialog();
        return;
    }

    if (IsKeyPressed(KEY_TAB))
    {
        const int dir = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? -1 : 1;
        authoringFocusField = (authoringFocusField + dir + 3) % 3; // name / weight / API key
    }

    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        if (authoringFocusField == 0 && codepoint >= 32)
            appendUtf8Codepoint(authoringPayload.name, codepoint);
        else if (authoringFocusField == 1)
        {
            if ((codepoint >= '0' && codepoint <= '9') || codepoint == '.' || codepoint == '-')
                appendUtf8Codepoint(authoringWeightBuffer, codepoint);
        }
        else if (authoringFocusField == 2 && codepoint >= 32)
            appendUtf8Codepoint(authoringAiApiKey, codepoint);
        codepoint = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE))
    {
        if (authoringFocusField == 0 && !authoringPayload.name.empty())
            authoringPayload.name.pop_back();
        else if (authoringFocusField == 1 && !authoringWeightBuffer.empty())
            authoringWeightBuffer.pop_back();
        else if (authoringFocusField == 2 && !authoringAiApiKey.empty())
            authoringAiApiKey.pop_back();
    }

    // Paste into the session API key field (Ctrl/Cmd+V).
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V) && authoringFocusField == 2)
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            // Trim whitespace/newlines from pasted keys.
            std::string pasted = clip;
            while (!pasted.empty()
                   && (pasted.back() == '\n' || pasted.back() == '\r'
                       || pasted.back() == ' ' || pasted.back() == '\t'))
                pasted.pop_back();
            size_t start = 0;
            while (start < pasted.size()
                   && (pasted[start] == ' ' || pasted[start] == '\t'
                       || pasted[start] == '\n' || pasted[start] == '\r'))
                ++start;
            if (start > 0)
                pasted = pasted.substr(start);
            authoringAiApiKey = pasted;
        }
    }

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        && IsKeyPressed(KEY_ENTER))
        commitAuthoringDialog();
}

void ItemEditor::drawAuthoringDialog(int screenWidth, int screenHeight)
{
    if (!authoringDialogOpen)
        return;

    pollAuthoringGenerateResult();

    if (authoringWaitMouseRelease && !IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        authoringWaitMouseRelease = false;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Vector2 mouse = GetMousePosition();
    const bool generateBusy = authoringGenerateBusy.load();
    const bool canClick =
        !authoringWaitMouseRelease
        && authoringIgnoreInputFrames <= 0
        && !subEditOpen
        && !generateBusy
        && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    auto drawWorkingLabel = [&](Rectangle afterBtn) {
        if (!generateBusy)
            return;
        // Slow red pulse (about 1.2s cycle).
        const float pulse =
            0.45f + 0.55f * (0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 5.0f));
        const Color red = {
            220,
            static_cast<unsigned char>(40 + 40 * (1.0f - pulse)),
            static_cast<unsigned char>(40 + 40 * (1.0f - pulse)),
            static_cast<unsigned char>(120 + 135 * pulse)};
        DrawTextEx(
            font,
            "Working",
            {afterBtn.x + afterBtn.width + 10.0f,
             afterBtn.y + (afterBtn.height - kFontSmall) * 0.5f},
            kFontSmall,
            1.0f,
            red);
    };

    DrawRectangle(0, 0, screenWidth, screenHeight, kModalOverlay);

    // ~50% wider than the original 640 form so a preview column fits on the right.
    const float dialogW = std::min(960.0f, static_cast<float>(screenWidth) - 32.0f);
    const float dialogH = std::min(680.0f, static_cast<float>(screenHeight) - 32.0f);
    const Rectangle dialog = {
        (static_cast<float>(screenWidth) - dialogW) * 0.5f,
        (static_cast<float>(screenHeight) - dialogH) * 0.5f + 8.0f,
        dialogW,
        dialogH};

    const float previewColW = std::min(300.0f, dialogW * 0.32f);
    const float formColW = dialogW - previewColW - 12.0f;

    // ID badge — centered on the full dialog width.
    const float badgeW = dialogW / 3.0f;
    const Rectangle idBadge = {
        dialog.x + (dialogW - badgeW) * 0.5f,
        dialog.y - kIdBadgeHeight * 0.5f,
        badgeW,
        kIdBadgeHeight};
    const std::string idDisplay = authoringIsModify
        ? authoringPayload.id
        : (authoringPayload.name.empty()
               ? "new_item"
               : slugifyItemId(authoringPayload.name));
    const std::string idShown =
        truncateToWidth(font, idDisplay, kIdBadgeFont, badgeW - 16.0f);
    const Vector2 idSize = MeasureTextEx(font, idShown.c_str(), kIdBadgeFont, 1.0f);

    DrawRectangleRounded(dialog, 0.03f, 8, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);
    DrawRectangleRounded(idBadge, 0.25f, 6, kModalFill);
    DrawRectangleLinesEx(idBadge, 1.5f, kPanelBorder);
    DrawTextEx(
        font,
        idShown.c_str(),
        {idBadge.x + (badgeW - idSize.x) * 0.5f,
         idBadge.y + (kIdBadgeHeight - idSize.y) * 0.5f},
        kIdBadgeFont,
        1.0f,
        kTextPrimary);

    DrawTextEx(
        font,
        authoringIsModify ? "Modify Item" : "New Item",
        {dialog.x + 20.0f, dialog.y + 18.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        authoringIsModify
            ? "Id is fixed  ·  Ctrl+Enter to save"
            : "Id is derived from name  ·  Ctrl+Enter to create",
        {dialog.x + 20.0f, dialog.y + 42.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    // Left form column (scrollable); right preview column is fixed.
    const Rectangle content = {
        dialog.x + 4.0f,
        dialog.y + kAuthoringHeaderH,
        formColW - 8.0f - kScrollBarSize,
        dialogH - kAuthoringHeaderH - kAuthoringFooterH};
    const float fieldX = dialog.x + 20.0f;
    const float fieldW = formColW - 40.0f - kScrollBarSize;
    const float fieldH = 26.0f;
    const Rectangle previewPane = {
        dialog.x + formColW,
        dialog.y + kAuthoringHeaderH,
        previewColW - 10.0f,
        dialogH - kAuthoringHeaderH - kAuthoringFooterH};

    struct Hit
    {
        enum class Kind
        {
            FocusField,
            CapToggle,
            DescTtsSwitch,
            RecipeSwitch,
            RecipeTtsSwitch,
            RecipeAdvanced,
            Dropdown1,
            Dropdown2,
            OpenDescription,
            OpenTtsDescription,
            OpenConstruction,
            OpenTtsConstruction,
            AiAssistConstruction,
            AiAssistTtsConstruction,
            AiAssistTtsDescription,
            OpenImagePath,
            OpenIconPath,
            OpenExamineSound,
            OpenUseSound,
            AiAssistImage,
            AiAssistIcon,
            AiAssistExamineSound,
            AiAssistUseSound,
            FocusApiKey,
            GenerateAssetsNow
        } kind;
        Rectangle rect{};
        int index = 0;
    };
    std::vector<Hit> hits;

    auto virt = [&](float y) { return content.y + y - authoringScrollY; };

    // Content height estimate
    float contentHeight = 0.0f;
    {
        float y = 0.0f;
        y += 14.0f + fieldH + 8.0f; // name/weight
        y += 14.0f + 36.0f + 8.0f; // description
        y += 28.0f; // TTS switch
        if (authoringPayload.descriptionTtsEnabled)
            y += 14.0f + 36.0f + 8.0f; // TTS description
        y += 22.0f + 4 * 22.0f + 10.0f; // capabilities
        y += 22.0f + 14.0f + fieldH + 8.0f; // AI assist header + API key
        y += 4 * (14.0f + 36.0f + 8.0f) + 12.0f; // AI assist path rows
        y += 54.0f + 58.0f + 78.0f; // Generate all + hint box + status box
        y += 28.0f; // product recipe switch
        if (authoringPayload.recipe.enabled)
        {
            y += 14.0f + fieldH + 8.0f;
            y += 14.0f + fieldH + 8.0f;
            y += 14.0f + 36.0f + 8.0f; // construction
            y += 28.0f; // construction TTS switch
            if (authoringPayload.recipe.ttsEnabled)
                y += 14.0f + 36.0f + 8.0f;
            y += 34.0f; // advanced button
            if (authoringRecipeAdvanced)
                y += 14.0f + 48.0f + 8.0f; // advanced JSON preview
        }
        y += 24.0f;
        contentHeight = y;
    }
    const float maxScroll = std::max(0.0f, contentHeight - content.height);
    authoringScrollY = std::clamp(authoringScrollY, 0.0f, maxScroll);

    if (CheckCollisionPointRec(mouse, content) && !subEditOpen && authoringDropdown == 0)
    {
        authoringScrollY -= GetMouseWheelMove() * 28.0f;
        authoringScrollY = std::clamp(authoringScrollY, 0.0f, maxScroll);
    }

    const Rectangle scrollTrack = {
        dialog.x + formColW - 8.0f - kScrollBarSize,
        content.y,
        kScrollBarSize,
        content.height};
    DrawRectangleRec(scrollTrack, kScrollTrack);
    if (maxScroll > 0.0f)
    {
        const float thumbH = std::max(24.0f, content.height * content.height / contentHeight);
        const float thumbY =
            content.y + (content.height - thumbH) * (authoringScrollY / maxScroll);
        const Rectangle thumb = {scrollTrack.x + 2.0f, thumbY, kScrollBarSize - 4.0f, thumbH};
        DrawRectangleRec(thumb, kScrollThumb);
        if (canClick && CheckCollisionPointRec(mouse, thumb))
            authoringDraggingScroll = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            authoringDraggingScroll = false;
        if (authoringDraggingScroll)
        {
            const float rel = (mouse.y - content.y - thumbH * 0.5f)
                / std::max(1.0f, content.height - thumbH);
            authoringScrollY = std::clamp(rel, 0.0f, 1.0f) * maxScroll;
        }
    }

    BeginScissorMode(
        static_cast<int>(content.x),
        static_cast<int>(content.y),
        static_cast<int>(content.width),
        static_cast<int>(content.height));

    float layoutY = 0.0f;

    // Name + weight
    {
        DrawTextEx(font, "Name *", {fieldX, virt(layoutY)}, kFontTiny, 1.0f, kTextMuted);
        DrawTextEx(
            font,
            "Weight (lb) *",
            {fieldX + fieldW * 0.62f, virt(layoutY)},
            kFontTiny,
            1.0f,
            kTextMuted);
        layoutY += 14.0f;
        const float nameW = fieldW * 0.58f;
        const float weightW = fieldW * 0.36f;
        const Rectangle nameField = {fieldX, virt(layoutY), nameW, fieldH};
        const Rectangle weightField = {fieldX + fieldW - weightW, virt(layoutY), weightW, fieldH};
        for (int pass = 0; pass < 2; ++pass)
        {
            const Rectangle field = pass == 0 ? nameField : weightField;
            const int idx = pass == 0 ? 0 : 1;
            const std::string& value =
                pass == 0 ? authoringPayload.name : authoringWeightBuffer;
            const bool focused = authoringFocusField == idx;
            DrawRectangleRec(field, Color{22, 20, 28, 255});
            DrawRectangleLinesEx(field, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
            const std::string shown = focused ? (value + "|") : value;
            DrawTextEx(
                font,
                shown.c_str(),
                {field.x + 6.0f, field.y + 5.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            hits.push_back({Hit::Kind::FocusField, field, idx});
        }
        layoutY += fieldH + 8.0f;
    }

    // Description (dialog only) — preview clipped to the box.
    {
        DrawTextEx(font, "Description *", {fieldX, virt(layoutY)}, kFontTiny, 1.0f, kTextMuted);
        layoutY += 14.0f;
        const Rectangle field = {fieldX, virt(layoutY), fieldW, 36.0f};
        DrawRectangleRec(field, Color{22, 20, 28, 255});
        DrawRectangleLinesEx(field, 1.0f, kPanelInnerEdge);
        drawClippedFieldPreview(
            font,
            field,
            content,
            authoringPayload.description,
            "(click to edit in dialog)",
            kFontSmall,
            2.0f);
        hits.push_back({Hit::Kind::OpenDescription, field, 0});
        layoutY += 36.0f + 8.0f;
    }

    // TTS switch under description
    {
        const Rectangle track = {fieldX, virt(layoutY), 56.0f, 22.0f};
        bool toggled = false;
        drawOnOffSwitch(
            font,
            track,
            authoringPayload.descriptionTtsEnabled,
            "TTS",
            false,
            toggled);
        hits.push_back({Hit::Kind::DescTtsSwitch, track, 0});
        layoutY += 28.0f;
    }

    if (authoringPayload.descriptionTtsEnabled)
    {
        const float aiBtnW = 88.0f;
        DrawTextEx(
            font, "TTS Description", {fieldX, virt(layoutY)}, kFontTiny, 1.0f, kTextMuted);
        layoutY += 14.0f;
        const Rectangle field = {fieldX, virt(layoutY), fieldW - aiBtnW - 10.0f, 36.0f};
        const Rectangle aiBtn = {
            fieldX + fieldW - aiBtnW, virt(layoutY) + 4.0f, aiBtnW, 28.0f};
        DrawRectangleRec(field, Color{22, 20, 28, 255});
        DrawRectangleLinesEx(field, 1.0f, kPanelInnerEdge);
        drawClippedFieldPreview(
            font,
            field,
            content,
            authoringPayload.ttsDescription,
            "(click to edit — TTS syntax highlighting)",
            kFontSmall,
            2.0f);
        drawEditorButton(font, aiBtn, "AI Assist", false, true);
        hits.push_back({Hit::Kind::OpenTtsDescription, field, 0});
        hits.push_back({Hit::Kind::AiAssistTtsDescription, aiBtn, 0});
        layoutY += 36.0f + 8.0f;
    }

    // Capabilities (no TTS checkbox)
    DrawTextEx(font, "Capabilities", {fieldX, virt(layoutY)}, kFontLabel, 1.0f, kTextPrimary);
    layoutY += 22.0f;
    struct CapRow
    {
        const char* label;
        bool* flag;
    };
    CapRow caps[] = {
        {"Stackable", &authoringPayload.capabilities.stackable},
        {"Tool (no consume on craft)", &authoringPayload.capabilities.tool},
        {"Light source", &authoringPayload.capabilities.lightSource},
        {"Consume on use", &authoringPayload.capabilities.consumeOnUse},
    };
    for (int i = 0; i < 4; ++i)
    {
        const Rectangle hit = {fieldX, virt(layoutY), fieldW * 0.55f, 20.0f};
        drawCheckboxRow(
            font, hit, caps[i].label, *caps[i].flag, CheckCollisionPointRec(mouse, hit));
        hits.push_back({Hit::Kind::CapToggle, hit, i});
        layoutY += 22.0f;
    }
    layoutY += 8.0f;

    // AI assist fields
    DrawTextEx(font, "AI assist", {fieldX, virt(layoutY)}, kFontLabel, 1.0f, kTextPrimary);
    layoutY += 22.0f;

    // Session-only API key (never written to disk / items.json).
    {
        DrawTextEx(
            font,
            "xAI API key (session only — not saved)",
            {fieldX, virt(layoutY)},
            kFontTiny,
            1.0f,
            kTextMuted);
        layoutY += 14.0f;
        const Rectangle keyField = {fieldX, virt(layoutY), fieldW, fieldH};
        const bool focused = authoringFocusField == 2;
        DrawRectangleRec(keyField, Color{22, 20, 28, 255});
        DrawRectangleLinesEx(keyField, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
        std::string masked;
        if (authoringAiApiKey.empty())
        {
            masked = focused ? "|" : "(paste key for image generation)";
        }
        else
        {
            masked.assign(authoringAiApiKey.size(), '*');
            if (focused)
                masked.push_back('|');
        }
        BeginScissorMode(
            static_cast<int>(keyField.x + 2),
            static_cast<int>(keyField.y + 2),
            static_cast<int>(keyField.width - 4),
            static_cast<int>(keyField.height - 4));
        // Re-open content scissor after field scissor (raylib is not nested).
        DrawTextEx(
            font,
            masked.c_str(),
            {keyField.x + 6.0f, keyField.y + 5.0f},
            kFontSmall,
            1.0f,
            authoringAiApiKey.empty() && !focused ? kTextMuted : kTextPrimary);
        EndScissorMode();
        BeginScissorMode(
            static_cast<int>(content.x),
            static_cast<int>(content.y),
            static_cast<int>(content.width),
            static_cast<int>(content.height));
        hits.push_back({Hit::Kind::FocusApiKey, keyField, 2});
        layoutY += fieldH + 8.0f;
    }

    {
        const float aiBtnW = 88.0f;
        const float rowH = 36.0f;
        struct AiFieldRow
        {
            const char* label;
            const std::string* value;
            Hit::Kind openKind;
            Hit::Kind assistKind;
            const char* emptyHint;
            int generateTarget; // 1..4
        };
        const AiFieldRow aiFields[] = {
            {"Examine image path",
             &authoringPayload.imagePath,
             Hit::Kind::OpenImagePath,
             Hit::Kind::AiAssistImage,
             "(path — click to edit)",
             1},
            {"Icon path",
             &authoringPayload.iconPath,
             Hit::Kind::OpenIconPath,
             Hit::Kind::AiAssistIcon,
             "(path — click to edit)",
             2},
            {"Examine sound",
             &authoringPayload.examineSoundPath,
             Hit::Kind::OpenExamineSound,
             Hit::Kind::AiAssistExamineSound,
             "(SFX path — click to edit)",
             3},
            {"Use sound",
             &authoringPayload.useSoundPath,
             Hit::Kind::OpenUseSound,
             Hit::Kind::AiAssistUseSound,
             "(SFX path — click to edit)",
             4},
        };
        // Leave room to the right of Generate for the pulsing "Working" label.
        const float workLabelW = 72.0f;
        const float rowFieldW = fieldW - aiBtnW - workLabelW - 12.0f;
        const int busyTarget = authoringGenerateTarget.load();
        for (const AiFieldRow& row : aiFields)
        {
            DrawTextEx(font, row.label, {fieldX, virt(layoutY)}, kFontTiny, 1.0f, kTextMuted);
            layoutY += 14.0f;
            const Rectangle field = {fieldX, virt(layoutY), rowFieldW, rowH};
            const Rectangle aiBtn = {
                fieldX + rowFieldW + 8.0f, virt(layoutY) + 4.0f, aiBtnW, 28.0f};
            DrawRectangleRec(field, Color{22, 20, 28, 255});
            DrawRectangleLinesEx(field, 1.0f, kPanelInnerEdge);
            // Show path truncated/wrapped inside the box only.
            const std::string pathShown = isPlausibleResourcePath(*row.value)
                ? *row.value
                : std::string();
            drawClippedFieldPreview(
                font,
                field,
                content,
                pathShown,
                row.emptyHint,
                kFontSmall,
                2.0f);
            drawEditorButton(
                font, aiBtn, "Generate", true, !generateBusy);
            if (generateBusy
                && (busyTarget == row.generateTarget || busyTarget == 5))
                drawWorkingLabel(aiBtn);
            hits.push_back({row.openKind, field, 0});
            hits.push_back({row.assistKind, aiBtn, 0});
            layoutY += rowH + 8.0f;
        }

        // Generate all — full form width (minus Working label), taller + wrapped label.
        {
            const float genAllH = 44.0f;
            const Rectangle genAllBtn = {
                fieldX,
                virt(layoutY),
                fieldW - workLabelW - 8.0f,
                genAllH};
            const Color fill = generateBusy
                ? kButtonDisabled
                : kPanelAccent;
            DrawRectangleRec(genAllBtn, fill);
            DrawRectangleLinesEx(genAllBtn, 1.0f, kPanelBorder);
            drawClippedFieldPreview(
                font,
                genAllBtn,
                content,
                "Generate all assets",
                "",
                kFontBody,
                3.0f);
            if (generateBusy && busyTarget == 5)
                drawWorkingLabel(genAllBtn);
            hits.push_back({Hit::Kind::GenerateAssetsNow, genAllBtn, 0});
            layoutY += genAllH + 10.0f;
        }

        // Hint + status, both clipped/wrapped inside taller boxes.
        {
            const Rectangle hintBox = {fieldX, virt(layoutY), fieldW, 52.0f};
            DrawRectangleRec(hintBox, Color{18, 16, 24, 255});
            DrawRectangleLinesEx(hintBox, 1.0f, kPanelInnerEdge);
            drawClippedFieldPreview(
                font,
                hintBox,
                content,
                "Generate writes PNG/MP3 now (API key required for images). "
                "Save writes items.json. Paths must stay under resources/.",
                "",
                kFontTiny,
                2.0f);
            layoutY += 58.0f;
        }
        if (!lastAuthoringStatus.empty())
        {
            const Rectangle statusBox = {fieldX, virt(layoutY), fieldW, 72.0f};
            DrawRectangleRec(statusBox, Color{18, 16, 24, 255});
            DrawRectangleLinesEx(statusBox, 1.0f, kPanelBorder);
            drawClippedFieldPreview(
                font,
                statusBox,
                content,
                lastAuthoringStatus,
                "",
                kFontTiny,
                2.0f);
            layoutY += 78.0f;
        }
    }
    layoutY += 6.0f;

    // Product Recipe
    {
        const Rectangle track = {fieldX, virt(layoutY), 56.0f, 22.0f};
        bool toggled = false;
        drawOnOffSwitch(font, track, authoringPayload.recipe.enabled, "Product Recipe", false, toggled);
        hits.push_back({Hit::Kind::RecipeSwitch, track, 0});
        layoutY += 28.0f;
    }

    if (authoringPayload.recipe.enabled)
    {
        DrawTextEx(font, "Item 1", {fieldX, virt(layoutY)}, kFontTiny, 1.0f, kTextMuted);
        layoutY += 14.0f;
        {
            const Rectangle field = {fieldX, virt(layoutY), fieldW, fieldH};
            DrawRectangleRec(field, Color{22, 20, 28, 255});
            DrawRectangleLinesEx(
                field, 1.0f, authoringDropdown == 1 ? kPanelBorder : kPanelInnerEdge);
            const std::string label = authoringPayload.recipe.component1.empty()
                ? "(select component)"
                : authoringPayload.recipe.component1;
            DrawTextEx(
                font,
                truncateToWidth(font, label, kFontSmall, fieldW - 28.0f).c_str(),
                {field.x + 6.0f, field.y + 5.0f},
                kFontSmall,
                1.0f,
                authoringPayload.recipe.component1.empty() ? kTextMuted : kTextPrimary);
            DrawTextEx(
                font, "▾", {field.x + field.width - 18.0f, field.y + 4.0f}, kFontSmall, 1.0f, kTextMuted);
            hits.push_back({Hit::Kind::Dropdown1, field, 0});
            authoringDropdown1Rect = field;
            layoutY += fieldH + 8.0f;
        }

        DrawTextEx(font, "Item 2", {fieldX, virt(layoutY)}, kFontTiny, 1.0f, kTextMuted);
        layoutY += 14.0f;
        {
            const Rectangle field = {fieldX, virt(layoutY), fieldW, fieldH};
            DrawRectangleRec(field, Color{22, 20, 28, 255});
            DrawRectangleLinesEx(
                field, 1.0f, authoringDropdown == 2 ? kPanelBorder : kPanelInnerEdge);
            const std::string label = authoringPayload.recipe.component2.empty()
                ? "(select component)"
                : authoringPayload.recipe.component2;
            DrawTextEx(
                font,
                truncateToWidth(font, label, kFontSmall, fieldW - 28.0f).c_str(),
                {field.x + 6.0f, field.y + 5.0f},
                kFontSmall,
                1.0f,
                authoringPayload.recipe.component2.empty() ? kTextMuted : kTextPrimary);
            DrawTextEx(
                font, "▾", {field.x + field.width - 18.0f, field.y + 4.0f}, kFontSmall, 1.0f, kTextMuted);
            hits.push_back({Hit::Kind::Dropdown2, field, 0});
            authoringDropdown2Rect = field;
            layoutY += fieldH + 8.0f;
        }

        // Construction Description + AI Assist
        {
            const float aiBtnW = 88.0f;
            DrawTextEx(
                font,
                "Construction Description",
                {fieldX, virt(layoutY)},
                kFontTiny,
                1.0f,
                kTextMuted);
            layoutY += 14.0f;
            const Rectangle field = {fieldX, virt(layoutY), fieldW - aiBtnW - 10.0f, 36.0f};
            const Rectangle aiBtn = {
                fieldX + fieldW - aiBtnW, virt(layoutY) + 4.0f, aiBtnW, 28.0f};
            DrawRectangleRec(field, Color{22, 20, 28, 255});
            DrawRectangleLinesEx(field, 1.0f, kPanelInnerEdge);
            drawClippedFieldPreview(
                font,
                field,
                content,
                authoringPayload.recipe.constructionDescription,
                "(click to edit in dialog)",
                kFontSmall,
                2.0f);
            drawEditorButton(font, aiBtn, "AI Assist", false, true);
            hits.push_back({Hit::Kind::OpenConstruction, field, 0});
            hits.push_back({Hit::Kind::AiAssistConstruction, aiBtn, 0});
            layoutY += 36.0f + 8.0f;
        }

        // TTS switch under construction
        {
            const Rectangle track = {fieldX, virt(layoutY), 56.0f, 22.0f};
            bool toggled = false;
            drawOnOffSwitch(
                font, track, authoringPayload.recipe.ttsEnabled, "TTS", false, toggled);
            hits.push_back({Hit::Kind::RecipeTtsSwitch, track, 0});
            layoutY += 28.0f;
        }

        if (authoringPayload.recipe.ttsEnabled)
        {
            const float aiBtnW = 88.0f;
            DrawTextEx(
                font,
                "TTS Construction Description",
                {fieldX, virt(layoutY)},
                kFontTiny,
                1.0f,
                kTextMuted);
            layoutY += 14.0f;
            const Rectangle field = {fieldX, virt(layoutY), fieldW - aiBtnW - 10.0f, 36.0f};
            const Rectangle aiBtn = {
                fieldX + fieldW - aiBtnW, virt(layoutY) + 4.0f, aiBtnW, 28.0f};
            DrawRectangleRec(field, Color{22, 20, 28, 255});
            DrawRectangleLinesEx(field, 1.0f, kPanelInnerEdge);
            drawClippedFieldPreview(
                font,
                field,
                content,
                authoringPayload.recipe.ttsConstructionDescription,
                "(click to edit — TTS syntax highlighting)",
                kFontSmall,
                2.0f);
            drawEditorButton(font, aiBtn, "AI Assist", false, true);
            hits.push_back({Hit::Kind::OpenTtsConstruction, field, 0});
            hits.push_back({Hit::Kind::AiAssistTtsConstruction, aiBtn, 0});
            layoutY += 36.0f + 8.0f;
        }

        // Advanced (components JSON) — left justified at bottom of expansion
        {
            const Rectangle advBtn = {fieldX, virt(layoutY), 110.0f, 28.0f};
            drawEditorButton(
                font,
                advBtn,
                authoringRecipeAdvanced ? "Advanced ▾" : "Advanced",
                authoringRecipeAdvanced,
                true);
            hits.push_back({Hit::Kind::RecipeAdvanced, advBtn, 0});
            layoutY += 34.0f;
            if (authoringRecipeAdvanced)
            {
                DrawTextEx(
                    font,
                    "Components JSON (advanced — click to edit)",
                    {fieldX, virt(layoutY)},
                    kFontTiny,
                    1.0f,
                    kTextMuted);
                layoutY += 14.0f;
                const Rectangle field = {fieldX, virt(layoutY), fieldW, 48.0f};
                DrawRectangleRec(field, Color{22, 20, 28, 255});
                DrawRectangleLinesEx(field, 1.0f, kPanelInnerEdge);
                drawClippedFieldPreview(
                    font,
                    field,
                    content,
                    authoringPayload.recipe.advancedComponentsJson,
                    "[ ]",
                    kFontTiny,
                    2.0f);
                hits.push_back({Hit::Kind::RecipeAdvanced, field, 1}); // open editor
                layoutY += 48.0f + 8.0f;
            }
        }
    }

    EndScissorMode();

    // Right-side image / icon / sound preview (outside form scissor).
    drawAuthoringPreviewPane(font, previewPane, canClick);

    // Dropdown under field
    if (authoringDropdown != 0 && !subEditOpen)
    {
        const std::vector<std::string> candidates = componentCandidateIds();
        const float rowH = 22.0f;
        const float menuH =
            std::min(200.0f, rowH * static_cast<float>(std::max<size_t>(candidates.size(), 1)) + 8.0f);
        const Rectangle anchor =
            authoringDropdown == 1 ? authoringDropdown1Rect : authoringDropdown2Rect;
        float menuY = anchor.y + anchor.height + 2.0f;
        if (menuY + menuH > dialog.y + dialogH - kAuthoringFooterH)
            menuY = std::max(dialog.y + kAuthoringHeaderH, anchor.y - menuH - 2.0f);
        const Rectangle menu = {
            anchor.width > 1.0f ? anchor.x : fieldX,
            menuY,
            anchor.width > 1.0f ? anchor.width : fieldW,
            menuH};
        DrawRectangleRec(menu, Color{28, 26, 36, 255});
        DrawRectangleLinesEx(menu, 1.0f, kPanelBorder);
        BeginScissorMode(
            static_cast<int>(menu.x),
            static_cast<int>(menu.y),
            static_cast<int>(menu.width),
            static_cast<int>(menu.height));
        float my = menu.y + 4.0f - authoringDropdownScroll;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const Rectangle row = {menu.x + 2.0f, my, menu.width - 4.0f, rowH};
            const bool hovered =
                CheckCollisionPointRec(mouse, row) && CheckCollisionPointRec(mouse, menu);
            if (hovered)
                DrawRectangleRec(row, kSelection);
            DrawTextEx(
                font,
                candidates[i].c_str(),
                {row.x + 6.0f, row.y + 3.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            if (canClick && hovered)
            {
                if (authoringDropdown == 1)
                    authoringPayload.recipe.component1 = candidates[i];
                else
                    authoringPayload.recipe.component2 = candidates[i];
                authoringDropdown = 0;
            }
            my += rowH;
        }
        EndScissorMode();
        if (CheckCollisionPointRec(mouse, menu))
        {
            authoringDropdownScroll -= GetMouseWheelMove() * rowH * 2.0f;
            if (authoringDropdownScroll < 0.0f)
                authoringDropdownScroll = 0.0f;
        }
        if (canClick && !CheckCollisionPointRec(mouse, menu))
            authoringDropdown = 0;
    }

    if (!authoringError.empty())
    {
        drawWrappedText(
            font,
            authoringError,
            {dialog.x + 20.0f, dialog.y + dialogH - kAuthoringFooterH + 4.0f},
            dialogW - 40.0f,
            kFontTiny,
            2.0f,
            Color{220, 100, 90, 255});
    }

    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const float btnY = dialog.y + dialogH - btnH - 14.0f;
    const Rectangle saveBtn = {dialog.x + dialogW - btnW * 2.0f - 32.0f, btnY, btnW, btnH};
    const Rectangle cancelBtn = {dialog.x + dialogW - btnW - 20.0f, btnY, btnW, btnH};
    drawEditorButton(
        font, saveBtn, authoringIsModify ? "Save" : "Create", true, true);
    drawEditorButton(font, cancelBtn, "Cancel", false, true);

    if (canClick && authoringDropdown == 0)
    {
        if (CheckCollisionPointRec(mouse, saveBtn))
            commitAuthoringDialog();
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeAuthoringDialog();
        else if (!CheckCollisionPointRec(mouse, dialog)
                 && !CheckCollisionPointRec(mouse, idBadge))
            closeAuthoringDialog();
        else if (CheckCollisionPointRec(mouse, content))
        {
            for (const Hit& hit : hits)
            {
                if (!CheckCollisionPointRec(mouse, hit.rect))
                    continue;
                switch (hit.kind)
                {
                case Hit::Kind::FocusField:
                    authoringFocusField = hit.index;
                    break;
                case Hit::Kind::FocusApiKey:
                    authoringFocusField = 2;
                    break;
                case Hit::Kind::CapToggle:
                    if (hit.index == 0)
                        authoringPayload.capabilities.stackable =
                            !authoringPayload.capabilities.stackable;
                    else if (hit.index == 1)
                        authoringPayload.capabilities.tool =
                            !authoringPayload.capabilities.tool;
                    else if (hit.index == 2)
                        authoringPayload.capabilities.lightSource =
                            !authoringPayload.capabilities.lightSource;
                    else if (hit.index == 3)
                        authoringPayload.capabilities.consumeOnUse =
                            !authoringPayload.capabilities.consumeOnUse;
                    break;
                case Hit::Kind::DescTtsSwitch:
                    authoringPayload.descriptionTtsEnabled =
                        !authoringPayload.descriptionTtsEnabled;
                    break;
                case Hit::Kind::RecipeSwitch:
                    authoringPayload.recipe.enabled = !authoringPayload.recipe.enabled;
                    if (!authoringPayload.recipe.enabled)
                        authoringRecipeAdvanced = false;
                    break;
                case Hit::Kind::RecipeTtsSwitch:
                    authoringPayload.recipe.ttsEnabled =
                        !authoringPayload.recipe.ttsEnabled;
                    break;
                case Hit::Kind::RecipeAdvanced:
                    if (hit.index == 0)
                        authoringRecipeAdvanced = !authoringRecipeAdvanced;
                    else
                        openSubEdit(SubEditKind::RecipeAdvancedJson);
                    break;
                case Hit::Kind::Dropdown1:
                    authoringDropdown = 1;
                    authoringDropdownScroll = 0.0f;
                    break;
                case Hit::Kind::Dropdown2:
                    authoringDropdown = 2;
                    authoringDropdownScroll = 0.0f;
                    break;
                case Hit::Kind::OpenDescription:
                    openSubEdit(SubEditKind::Description);
                    break;
                case Hit::Kind::OpenTtsDescription:
                    openSubEdit(SubEditKind::TtsDescription);
                    break;
                case Hit::Kind::OpenConstruction:
                    openSubEdit(SubEditKind::ConstructionDescription);
                    break;
                case Hit::Kind::OpenTtsConstruction:
                    openSubEdit(SubEditKind::TtsConstructionDescription);
                    break;
                case Hit::Kind::AiAssistConstruction:
                    authoringPayload.aiAssist.assistConstructionDescription = true;
                    if (authoringPayload.recipe.constructionDescription.empty())
                        authoringPayload.recipe.constructionDescription =
                            "You combine the parts into " + authoringPayload.name + ".";
                    lastAuthoringStatus = "AI Assist: construction description seeded.";
                    break;
                case Hit::Kind::AiAssistTtsConstruction:
                    authoringPayload.aiAssist.assistTtsConstructionDescription = true;
                    if (authoringPayload.recipe.ttsConstructionDescription.empty())
                        authoringPayload.recipe.ttsConstructionDescription =
                            !authoringPayload.recipe.constructionDescription.empty()
                            ? authoringPayload.recipe.constructionDescription
                            : ("You assemble the " + authoringPayload.name + ".");
                    lastAuthoringStatus = "AI Assist: TTS construction seeded.";
                    break;
                case Hit::Kind::AiAssistTtsDescription:
                    authoringPayload.aiAssist.assistTtsDescription = true;
                    if (authoringPayload.ttsDescription.empty())
                        authoringPayload.ttsDescription = authoringPayload.description;
                    lastAuthoringStatus = "AI Assist: TTS description seeded.";
                    break;
                case Hit::Kind::OpenImagePath:
                    openSubEdit(SubEditKind::ImagePath);
                    break;
                case Hit::Kind::OpenIconPath:
                    openSubEdit(SubEditKind::IconPath);
                    break;
                case Hit::Kind::OpenExamineSound:
                    openSubEdit(SubEditKind::ExamineSound);
                    break;
                case Hit::Kind::OpenUseSound:
                    openSubEdit(SubEditKind::UseSound);
                    break;
                case Hit::Kind::AiAssistImage:
                    generateAuthoringAssetsNow(1);
                    break;
                case Hit::Kind::AiAssistIcon:
                    generateAuthoringAssetsNow(2);
                    break;
                case Hit::Kind::AiAssistExamineSound:
                    generateAuthoringAssetsNow(3);
                    break;
                case Hit::Kind::AiAssistUseSound:
                    generateAuthoringAssetsNow(4);
                    break;
                case Hit::Kind::GenerateAssetsNow:
                    generateAuthoringAssetsNow(5);
                    break;
                }
                break;
            }
        }
    }

    if (subEditOpen)
        drawSubEditDialog(screenWidth, screenHeight);
}

} // namespace timberline_editor
