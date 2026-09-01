/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Raised/depressed editor buttons with config-driven min/max and word wrap.
 ******************************************************************************/

#include "EditorButton.h"
#include "EditorInput.h"

#include "EditorTheme.h"
#include "ImageCompression.h"
#include "PlatformPath.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

Color colorFromJson(const nlohmann::json& arr, Color fallback)
{
    if (!arr.is_array() || arr.size() < 3)
        return fallback;
    Color c = fallback;
    try
    {
        c.r = static_cast<unsigned char>(arr.at(0).get<int>());
        c.g = static_cast<unsigned char>(arr.at(1).get<int>());
        c.b = static_cast<unsigned char>(arr.at(2).get<int>());
        if (arr.size() >= 4)
            c.a = static_cast<unsigned char>(arr.at(3).get<int>());
    }
    catch (...)
    {
        return fallback;
    }
    return c;
}

bool loadTextureFromRoots(
    const std::string& relPath,
    const std::string& resourceDir,
    const std::string& assetRoot,
    Texture2D& out)
{
    std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relPath);
    if (!resourceDir.empty())
    {
        std::string stripped = relPath;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        paths.push_back(pathJoin(resourceDir, stripped));
        paths.push_back(pathJoin(resourceDir, relPath));
    }
    for (const std::string& path : paths)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str())
            && loadTextureFromAssetFile(compressed, out))
            return true;
        if (FileExists(path.c_str()) && loadTextureFromAssetFile(path, out))
            return true;
    }
    return false;
}

std::vector<std::string> wrapLabelLines(
    Font font,
    const std::string& text,
    float fontSize,
    float maxWidth,
    bool wordWrap)
{
    std::vector<std::string> lines;
    if (text.empty())
    {
        lines.push_back("");
        return lines;
    }
    if (!wordWrap)
    {
        // Single line, caller may ellipsize later.
        lines.push_back(text);
        return lines;
    }

    std::string line;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word)
    {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 1.0f).x <= maxWidth
            || line.empty())
        {
            // If a single word exceeds maxWidth, keep it (layout will ellipsize).
            line = candidate;
            if (MeasureTextEx(font, line.c_str(), fontSize, 1.0f).x > maxWidth
                && line.find(' ') == std::string::npos)
            {
                lines.push_back(line);
                line.clear();
            }
            continue;
        }
        lines.push_back(line);
        line = word;
    }
    if (!line.empty())
        lines.push_back(line);
    if (lines.empty())
        lines.push_back("");
    return lines;
}

std::string ellipsizeLine(Font font, const std::string& text, float fontSize, float maxWidth)
{
    if (text.empty()
        || MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x <= maxWidth)
        return text;
    std::string out = text;
    const std::string ellipsis = "…";
    while (!out.empty()
           && MeasureTextEx(font, (out + ellipsis).c_str(), fontSize, 1.0f).x > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

void drawProceduralButton(Rectangle bounds, bool pressed, bool enabled, bool accent)
{
    Color fill = !enabled
        ? kButtonDisabled
        : (accent ? kPanelAccent : Color{44, 42, 52, 255});
    if (pressed && enabled)
    {
        fill.r = static_cast<unsigned char>(fill.r * 0.75f);
        fill.g = static_cast<unsigned char>(fill.g * 0.75f);
        fill.b = static_cast<unsigned char>(fill.b * 0.75f);
    }
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, 1.0f, kPanelBorder);

    // Bevel: raised = light top/left; depressed = invert.
    const Color light = {200, 180, 120, 180};
    const Color dark = {20, 16, 24, 200};
    const Color top = pressed ? dark : light;
    const Color bot = pressed ? light : dark;
    DrawLineEx(
        {bounds.x + 1, bounds.y + 1},
        {bounds.x + bounds.width - 1, bounds.y + 1},
        1.0f,
        top);
    DrawLineEx(
        {bounds.x + 1, bounds.y + 1},
        {bounds.x + 1, bounds.y + bounds.height - 1},
        1.0f,
        top);
    DrawLineEx(
        {bounds.x + 1, bounds.y + bounds.height - 1},
        {bounds.x + bounds.width - 1, bounds.y + bounds.height - 1},
        1.0f,
        bot);
    DrawLineEx(
        {bounds.x + bounds.width - 1, bounds.y + 1},
        {bounds.x + bounds.width - 1, bounds.y + bounds.height - 1},
        1.0f,
        bot);
}

void drawSkinnedButton(
    const EditorButtonResources& res,
    Rectangle bounds,
    bool pressed,
    bool enabled,
    bool accent)
{
    const Texture2D& tex = (pressed && res.depressedLoaded)
        ? res.depressed
        : res.raised;
    const bool have = (pressed ? res.depressedLoaded : res.raisedLoaded)
        || res.raisedLoaded;
    const Texture2D& useTex =
        (have && tex.id != 0) ? tex : res.raised;
    if (!have || useTex.id == 0)
    {
        drawProceduralButton(bounds, pressed, enabled, accent);
        return;
    }

    const EditorButtonConfig& c = res.config;
    NPatchInfo nine{};
    nine.source = {
        0,
        0,
        static_cast<float>(useTex.width),
        static_cast<float>(useTex.height)};
    nine.left = c.sliceLeft;
    nine.top = c.sliceTop;
    nine.right = c.sliceRight;
    nine.bottom = c.sliceBottom;
    nine.layout = NPATCH_NINE_PATCH;

    Color tint = WHITE;
    if (!enabled)
        tint = Color{120, 120, 120, 200};
    else if (accent)
        // Warm lift so primary actions read against secondary buttons.
        tint = Color{255, 236, 196, 255};
    DrawTextureNPatch(useTex, nine, bounds, {0, 0}, 0.0f, tint);
}

} // namespace

EditorButtonResources& editorButtons()
{
    static EditorButtonResources instance;
    return instance;
}

bool saveEditorUiConfig(const std::string& resourceDir, const EditorButtonResources& res)
{
    if (resourceDir.empty())
        return false;

    nlohmann::json root = nlohmann::json::object();
    root["description"] =
        "Timberline resource editor UI metrics. Loaded at editor start; edit freely without recompiling.";

    root["textFields"] = {
        {"caretBlinkHz", res.caretBlinkHz},
        {"padX", res.textFieldPadX},
        {"padY", res.textFieldPadY},
        {"scrollGutter", res.textFieldScrollGutter},
    };

    root["workingOverlay"] = {
        {"_comment_sizePx",
         "Spinner texture MUST be exactly sizePx x sizePx pixels (currently 64). "
         "Loader rejects other sizes. May change later or move to SVG."},
        {"sizePx", res.working.sizePx},
        {"_comment_revolutionsPerSecond", "Clockwise spin rate of the working spinner."},
        {"revolutionsPerSecond", res.working.revolutionsPerSecond},
        {"spinnerPath", res.working.spinnerPath},
        {"title", res.working.title},
    };

    const EditorButtonConfig& c = res.config;
    root["buttons"] = {
        {"minWidth", c.minWidth},
        {"maxWidth", c.maxWidth},
        {"minHeight", c.minHeight},
        {"maxHeight", c.maxHeight},
        {"padX", c.padX},
        {"padY", c.padY},
        {"fontSize", c.fontSize},
        {"lineSpacing", c.lineSpacing},
        {"wordWrap", c.wordWrap},
        {"sliceLeft", c.sliceLeft},
        {"sliceTop", c.sliceTop},
        {"sliceRight", c.sliceRight},
        {"sliceBottom", c.sliceBottom},
        {"raisedSkin", c.raisedSkinPath},
        {"depressedSkin", c.depressedSkinPath},
        {"labelOffsetPressedX", c.labelOffsetPressedX},
        {"labelOffsetPressedY", c.labelOffsetPressedY},
    };

    const std::string configPath = pathJoin(resourceDir, "editor_ui_config.json");
    std::ofstream out(configPath.c_str());
    if (!out)
    {
        TraceLog(
            LOG_WARNING,
            "TIMBERLINE: failed to write editor_ui_config.json at %s",
            configPath.c_str());
        return false;
    }
    out << root.dump(2) << '\n';
    return static_cast<bool>(out);
}

void EditorButtonResources::load(
    const std::string& resourceDir,
    const std::string& assetRoot)
{
    unload();
    config = EditorButtonConfig{};

    const std::string configPath = pathJoin(resourceDir, "editor_ui_config.json");
    std::ifstream in(configPath.c_str());
    if (in)
    {
        try
        {
            nlohmann::json root;
            in >> root;
            const nlohmann::json& b =
                root.contains("buttons") && root["buttons"].is_object()
                ? root["buttons"]
                : root;
            config.minWidth = b.value("minWidth", config.minWidth);
            config.maxWidth = b.value("maxWidth", config.maxWidth);
            config.minHeight = b.value("minHeight", config.minHeight);
            config.maxHeight = b.value("maxHeight", config.maxHeight);
            config.padX = b.value("padX", config.padX);
            config.padY = b.value("padY", config.padY);
            config.fontSize = b.value("fontSize", config.fontSize);
            config.lineSpacing = b.value("lineSpacing", config.lineSpacing);
            config.wordWrap = b.value("wordWrap", config.wordWrap);
            config.sliceLeft = b.value("sliceLeft", config.sliceLeft);
            config.sliceTop = b.value("sliceTop", config.sliceTop);
            config.sliceRight = b.value("sliceRight", config.sliceRight);
            config.sliceBottom = b.value("sliceBottom", config.sliceBottom);
            config.raisedSkinPath =
                b.value("raisedSkin", config.raisedSkinPath);
            config.depressedSkinPath =
                b.value("depressedSkin", config.depressedSkinPath);
            config.labelOffsetPressedX =
                b.value("labelOffsetPressedX", config.labelOffsetPressedX);
            config.labelOffsetPressedY =
                b.value("labelOffsetPressedY", config.labelOffsetPressedY);

            const nlohmann::json& tf =
                root.contains("textFields") && root["textFields"].is_object()
                ? root["textFields"]
                : root;
            caretBlinkHz = tf.value("caretBlinkHz", caretBlinkHz);
            if (caretBlinkHz < 0.2f)
                caretBlinkHz = 0.2f;
            if (caretBlinkHz > 8.0f)
                caretBlinkHz = 8.0f;
            textFieldPadX = tf.value("padX", textFieldPadX);
            textFieldPadY = tf.value("padY", textFieldPadY);
            textFieldScrollGutter = tf.value("scrollGutter", textFieldScrollGutter);

            if (root.contains("workingOverlay") && root["workingOverlay"].is_object())
            {
                const nlohmann::json& w = root["workingOverlay"];
                working.sizePx = w.value("sizePx", working.sizePx);
                working.revolutionsPerSecond =
                    w.value("revolutionsPerSecond", working.revolutionsPerSecond);
                working.spinnerPath = w.value("spinnerPath", working.spinnerPath);
                working.title = w.value("title", working.title);
                if (working.sizePx < 16)
                    working.sizePx = 16;
                if (working.sizePx > 512)
                    working.sizePx = 512;
                if (working.revolutionsPerSecond < 0.05f)
                    working.revolutionsPerSecond = 0.05f;
                if (working.revolutionsPerSecond > 8.0f)
                    working.revolutionsPerSecond = 8.0f;
            }
            configLoaded = true;
        }
        catch (const nlohmann::json::exception& ex)
        {
            TraceLog(
                LOG_WARNING,
                "TIMBERLINE: editor_ui_config.json parse failed: %s",
                ex.what());
        }
    }
    else
    {
        TraceLog(
            LOG_INFO,
            "TIMBERLINE: no editor_ui_config.json — using button defaults");
    }

    raisedLoaded = loadTextureFromRoots(
        config.raisedSkinPath, resourceDir, assetRoot, raised);
    depressedLoaded = loadTextureFromRoots(
        config.depressedSkinPath, resourceDir, assetRoot, depressed);

    workingSpinnerLoaded = loadTextureFromRoots(
        working.spinnerPath, resourceDir, assetRoot, workingSpinner);
    if (workingSpinnerLoaded)
    {
        if (workingSpinner.width != working.sizePx
            || workingSpinner.height != working.sizePx)
        {
            TraceLog(
                LOG_WARNING,
                "TIMBERLINE: working spinner '%s' is %dx%d; config sizePx=%d — rejecting "
                "(dimensions are rigidly enforced)",
                working.spinnerPath.c_str(),
                workingSpinner.width,
                workingSpinner.height,
                working.sizePx);
            UnloadTexture(workingSpinner);
            workingSpinner = {};
            workingSpinnerLoaded = false;
        }
    }
    TraceLog(
        LOG_INFO,
        "TIMBERLINE: editor buttons skins raised=%s depressed=%s spinner=%s",
        raisedLoaded ? "ok" : "missing",
        depressedLoaded ? "ok" : "missing",
        workingSpinnerLoaded ? "ok" : "missing");
}

void EditorButtonResources::unload()
{
    if (raisedLoaded)
    {
        UnloadTexture(raised);
        raised = {};
        raisedLoaded = false;
    }
    if (depressedLoaded)
    {
        UnloadTexture(depressed);
        depressed = {};
        depressedLoaded = false;
    }
    if (workingSpinnerLoaded)
    {
        UnloadTexture(workingSpinner);
        workingSpinner = {};
        workingSpinnerLoaded = false;
    }
    configLoaded = false;
}

void EditorButton::layout(Font font, const EditorButtonConfig& config)
{
    clicked = false;
    float width = preferred.width;
    if (expandWidth)
        width = preferred.width;
    width = std::clamp(width, config.minWidth, config.maxWidth);

    const float textMaxW = std::max(8.0f, width - config.padX * 2.0f);
    std::vector<std::string> lines = wrapLabelLines(
        font, label, config.fontSize, textMaxW, config.wordWrap);

    // Fit height to lines, then clamp; ellipsize if still too tall.
    const float lineH = config.fontSize + config.lineSpacing;
    float textH = static_cast<float>(lines.size()) * lineH;
    float height = textH + config.padY * 2.0f;
    height = std::clamp(height, config.minHeight, config.maxHeight);

    const int maxLines = std::max(
        1,
        static_cast<int>((height - config.padY * 2.0f) / lineH));
    if (static_cast<int>(lines.size()) > maxLines)
    {
        lines.resize(static_cast<size_t>(maxLines));
        if (!lines.empty())
            lines.back() = ellipsizeLine(
                font, lines.back(), config.fontSize, textMaxW);
        textH = static_cast<float>(lines.size()) * lineH;
        height = std::clamp(
            textH + config.padY * 2.0f, config.minHeight, config.maxHeight);
    }

    bounds.height = height;

    if (expandWidth)
    {
        // Fill the preferred slot width (still respect absolute min).
        bounds.width = std::max(preferred.width, config.minWidth);
        bounds.x = preferred.x;
        bounds.y = preferred.y + std::max(0.0f, (preferred.height - height) * 0.5f);
        if (preferred.height < height)
            bounds.y = preferred.y;
    }
    else
    {
        bounds.width = width;
        bounds.x = preferred.x + (preferred.width - width) * 0.5f;
        bounds.y = preferred.y + (preferred.height - height) * 0.5f;
        if (preferred.height < height)
            bounds.y = preferred.y;
        if (preferred.width < width)
            bounds.x = preferred.x;
    }
}

bool EditorButton::update(const EditorButtonConfig& config)
{
    (void)config;
    clicked = false;
    const Rectangle hit = (bounds.width > 1.0f && bounds.height > 1.0f)
        ? bounds
        : preferred;
    const Vector2 mouse = GetMousePosition();
    hovered = enabled && CheckCollisionPointRec(mouse, hit);

    if (!enabled)
    {
        pressed = false;
        return false;
    }

    // Callers often construct EditorButton on the stack each frame, so there is
    // no durable press-tracking across frames. Use editorMouse* sticky edges so
    // brief macOS trackpad taps still register (raylib alone often misses them).
    pressed = hovered && editorMouseDown(MOUSE_BUTTON_LEFT);
    if (hovered && editorMousePressed(MOUSE_BUTTON_LEFT))
        clicked = true;

    return clicked;
}

void EditorButton::draw(Font font, const EditorButtonConfig& config) const
{
    const Rectangle r = (bounds.width > 1.0f && bounds.height > 1.0f)
        ? bounds
        : preferred;
    const bool showPressed = pressed && enabled;

    drawSkinnedButton(editorButtons(), r, showPressed, enabled, accent);

    const float textMaxW = std::max(8.0f, r.width - config.padX * 2.0f);
    std::vector<std::string> lines = wrapLabelLines(
        font, label, config.fontSize, textMaxW, config.wordWrap);
    const float lineH = config.fontSize + config.lineSpacing;
    const int maxLines = std::max(
        1, static_cast<int>((r.height - config.padY * 2.0f) / lineH));
    if (static_cast<int>(lines.size()) > maxLines)
    {
        lines.resize(static_cast<size_t>(maxLines));
        if (!lines.empty())
            lines.back() = ellipsizeLine(
                font, lines.back(), config.fontSize, textMaxW);
    }

    float textBlockH = static_cast<float>(lines.size()) * lineH;
    float y = r.y + (r.height - textBlockH) * 0.5f;
    float xOff = 0.0f;
    float yOff = 0.0f;
    if (showPressed)
    {
        xOff = config.labelOffsetPressedX;
        yOff = config.labelOffsetPressedY;
    }

    const Color labelColor = enabled ? kTextPrimary : kTextDisabled;
    // Do not use BeginScissorMode here — raylib scissor is not nested and would
    // cancel an outer dialog/content scissor. Layout already clamps text to size.
    for (const std::string& line : lines)
    {
        const Vector2 size =
            MeasureTextEx(font, line.c_str(), config.fontSize, 1.0f);
        const float x = r.x + (r.width - size.x) * 0.5f + xOff;
        DrawTextEx(
            font,
            line.c_str(),
            {x, y + yOff},
            config.fontSize,
            1.0f,
            labelColor);
        y += lineH;
    }
}

void drawEditorButton(
    Font font,
    Rectangle bounds,
    const char* label,
    bool accent,
    bool enabled)
{
    EditorButton btn;
    btn.preferred = bounds;
    btn.label = label ? label : "";
    btn.accent = accent;
    btn.enabled = enabled;
    btn.expandWidth = true;
    btn.layout(font, editorButtons().config);
    // Keep caller hit-rects stable: draw into the exact slot they pass.
    btn.bounds = bounds;
    const Vector2 mouse = GetMousePosition();
    btn.hovered = enabled && CheckCollisionPointRec(mouse, bounds);
    btn.pressed = btn.hovered && editorMouseDown(MOUSE_BUTTON_LEFT);
    btn.draw(font, editorButtons().config);
}

} // namespace timberline_editor
