#include "SceneOverlayDef.h"

#include <nlohmann/json.hpp>

namespace highline_ridge
{

namespace
{

bool parseHexColor(const std::string& text, Color& outColor)
{
    if (text.size() != 7 || text[0] != '#')
        return false;

    auto hexValue = [](char ch) -> int
    {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F')
            return 10 + (ch - 'A');
        return -1;
    };

    int r1 = hexValue(text[1]);
    int r2 = hexValue(text[2]);
    int g1 = hexValue(text[3]);
    int g2 = hexValue(text[4]);
    int b1 = hexValue(text[5]);
    int b2 = hexValue(text[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0)
        return false;

    outColor.r = (unsigned char)((r1 << 4) | r2);
    outColor.g = (unsigned char)((g1 << 4) | g2);
    outColor.b = (unsigned char)((b1 << 4) | b2);
    outColor.a = 255;
    return true;
}

OverlayEffectType parseOverlayEffectType(const std::string& type)
{
    if (type == "fade")
        return OverlayEffectType::Fade;
    return OverlayEffectType::Vignette;
}

OverlaySequenceAction parseOverlaySequenceAction(const std::string& action)
{
    if (action == "fadeTo" || action == "fade_to")
        return OverlaySequenceAction::FadeTo;
    if (action == "vignetteTo" || action == "vignette_to")
        return OverlaySequenceAction::VignetteTo;
    if (action == "hypoxiaTo" || action == "hypoxia_to"
        || action == "hypoxiaFadeIn" || action == "hypoxia_fade_in"
        || action == "hypoxiaFadeOut" || action == "hypoxia_fade_out")
    {
        return OverlaySequenceAction::HypoxiaTo;
    }
    return OverlaySequenceAction::Hold;
}

}

bool parseOverlayColor(const nlohmann::json& value, Color& outColor)
{
    if (value.is_string())
        return parseHexColor(value.get<std::string>(), outColor);

    if (value.is_array() && value.size() >= 3)
    {
        outColor.r = (unsigned char)value[0].get<int>();
        outColor.g = (unsigned char)value[1].get<int>();
        outColor.b = (unsigned char)value[2].get<int>();
        outColor.a = value.size() >= 4 ? (unsigned char)value[3].get<int>() : 255;
        return true;
    }

    return false;
}

bool parseOverlayAnimate(const nlohmann::json& animate, OverlayAnimateDef& out)
{
    out.enabled = false;
    out.targetOcclusionPercent = 0.0f;
    out.durationSeconds = 0.0f;

    if (!animate.is_object())
        return true;

    out.targetOcclusionPercent = animate.value(
        "occlusionPercent",
        animate.value("occlusion_percent", 0.0f));
    out.durationSeconds = animate.value(
        "durationSeconds",
        animate.value("duration_seconds", 0.0f));
    out.enabled = out.durationSeconds > 0.0f;
    return true;
}

bool parseSceneOverlay(const nlohmann::json& overlay, SceneOverlayDef& out)
{
    if (!overlay.is_object())
        return false;

    out.id = overlay.value("id", "");
    out.type = parseOverlayEffectType(overlay.value("type", "vignette"));
    out.occlusionPercent = overlay.value(
        "occlusionPercent",
        overlay.value("occlusion_percent", 0.0f));
    out.opacity = overlay.value("opacity", 0.0f);
    out.color = BLACK;
    parseOverlayColor(overlay.value("color", nlohmann::json()), out.color);
    out.fadeInSeconds = overlay.value(
        "fadeInSeconds",
        overlay.value("fade_in_seconds", 0.0f));
    out.fadeOutSeconds = overlay.value(
        "fadeOutSeconds",
        overlay.value("fade_out_seconds", 0.0f));

    if (!parseOverlayAnimate(overlay.value("animate", nlohmann::json::object()), out.animate))
        return false;

    return true;
}

bool parseSceneOverlays(const nlohmann::json& overlays, std::vector<SceneOverlayDef>& out)
{
    out.clear();
    if (!overlays.is_array())
        return true;

    for (const nlohmann::json& entry : overlays)
    {
        SceneOverlayDef parsed;
        if (!parseSceneOverlay(entry, parsed))
            return false;
        out.push_back(parsed);
    }

    return true;
}

bool parseOverlaySequenceStep(const nlohmann::json& step, OverlaySequenceStep& out)
{
    if (!step.is_object())
        return false;

    out.action = parseOverlaySequenceAction(step.value("action", "hold"));
    out.targetOpacity = step.value("opacity", step.value("targetOpacity", 0.0f));
    if (step.contains("fillPercent") || step.contains("fill_percent"))
    {
        const float fillPercent = step.value(
            "fillPercent",
            step.value("fill_percent", 0.0f));
        out.targetOpacity = fillPercent / 100.0f;
    }
    out.targetOcclusionPercent = step.value(
        "occlusionPercent",
        step.value("occlusion_percent", 0.0f));
    out.durationSeconds = step.value(
        "durationSeconds",
        step.value("duration_seconds", 0.0f));
    out.color = BLACK;
    parseOverlayColor(step.value("color", nlohmann::json()), out.color);
    out.sfxPath = step.value("sfx", step.value("sfxPath", step.value("sfx_path", "")));
    out.sfxVolume = step.value("sfxVolume", step.value("sfx_volume", 1.0f));
    return true;
}

bool parseOverlaySequence(const nlohmann::json& sequence, std::vector<OverlaySequenceStep>& out)
{
    out.clear();
    if (!sequence.is_array())
        return true;

    for (const nlohmann::json& entry : sequence)
    {
        OverlaySequenceStep parsed;
        if (!parseOverlaySequenceStep(entry, parsed))
            return false;
        out.push_back(parsed);
    }

    return true;
}

}