/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "EventAuthoringDialog.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace timberline_editor
{

namespace
{

constexpr int kFocusFlag = 1;
constexpr int kFocusHealth = 2;
constexpr int kFocusEnergy = 3;
constexpr int kFocusResolve = 4;
constexpr int kFocusLucidity = 5;
constexpr int kFocusCharisma = 6;
constexpr int kFocusMoney = 7;
constexpr int kFocusKey = 8;
constexpr int kFocusOnZero = 9;
constexpr int kFocusTabActor = 10;
constexpr int kFocusTabDelta = 11;
constexpr int kFocusOpinionBase = 100;
constexpr int kMaxOpinions = 16;

constexpr const char* kGrantStoryFlagSuffix = "/grantStoryFlag";
constexpr const char* kStatusSuffix = "/status";

std::string ellipsizeUi(
    Font font,
    const std::string& text,
    float fontSize,
    float maxWidth)
{
    if (text.empty()
        || MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x <= maxWidth)
        return text;
    std::string out = text;
    const std::string ellipsis = "...";
    while (!out.empty()
           && MeasureTextEx(font, (out + ellipsis).c_str(), fontSize, 1.0f).x > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

void drawClippedFieldText(
    Font font,
    const Rectangle& field,
    const std::string& text,
    const char* placeholder,
    float fontSize,
    Color color)
{
    BeginScissorMode(
        (int)field.x + 2, (int)field.y + 2, (int)field.width - 4, (int)field.height - 4);
    const std::string shown = text.empty()
        ? std::string(placeholder ? placeholder : "")
        : ellipsizeUi(font, text, fontSize, field.width - 16.0f);
    DrawTextEx(
        font,
        shown.c_str(),
        {field.x + 8.0f, field.y + 6.0f},
        fontSize,
        1.0f,
        color);
    EndScissorMode();
}

void insertUtf8(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
    char bytes[5] = {};
    int size = 0;
    if (codepoint < 0x80)
    {
        bytes[0] = static_cast<char>(codepoint);
        size = 1;
    }
    else if (codepoint <= 0x7FF)
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
    else
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    buffer.append(bytes, bytes + size);
}

void backspaceUtf8(std::string& buffer)
{
    if (buffer.empty())
        return;
    int i = static_cast<int>(buffer.size()) - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i));
}

bool pointerEndsWith(const std::string& pointer, const char* suffix)
{
    const size_t n = std::char_traits<char>::length(suffix);
    return pointer.size() >= n && pointer.compare(pointer.size() - n, n, suffix) == 0;
}

std::string choicePointerFrom(const std::string& pointer)
{
    if (pointerEndsWith(pointer, kGrantStoryFlagSuffix)
        || pointerEndsWith(pointer, kStatusSuffix))
    {
        const size_t slash = pointer.rfind('/');
        if (slash != std::string::npos)
            return pointer.substr(0, slash);
    }
    return pointer;
}

std::string sanitizeFlagId(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char ch : raw)
    {
        if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == ':')
            out.push_back(static_cast<char>(ch));
        else if (ch == ' ' || ch == '/' || ch == '.')
        {
            if (!out.empty() && out.back() != '_')
                out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

std::string formatSignedInt(int v)
{
    return std::to_string(v);
}

std::string formatFloat(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

void setOrEraseString(nlohmann::json& obj, const char* key, const std::string& value)
{
    if (value.empty())
        obj.erase(key);
    else
        obj[key] = value;
}

void setOrEraseFloat(nlohmann::json& obj, const char* key, float value)
{
    if (value == 0.0f)
        obj.erase(key);
    else
        obj[key] = value;
}

std::string readActorField(const nlohmann::json& status)
{
    const nlohmann::json& actorNode = status.value("actor", nlohmann::json());
    if (actorNode.is_string())
        return actorNode.get<std::string>();
    if (actorNode.is_object())
        return actorNode.value("id", std::string());
    return {};
}

int opinionRowFromField(int field)
{
    if (field < kFocusOpinionBase)
        return -1;
    return (field - kFocusOpinionBase) / 2;
}

bool fieldIsOpinionActor(int field)
{
    return field >= kFocusOpinionBase && ((field - kFocusOpinionBase) % 2) == 0;
}

bool fieldIsOpinionDelta(int field)
{
    return field >= kFocusOpinionBase && ((field - kFocusOpinionBase) % 2) == 1;
}

std::vector<EventOpinionRow> cleanedOpinions(const std::vector<EventOpinionRow>& in)
{
    std::vector<EventOpinionRow> out;
    out.reserve(in.size());
    for (const EventOpinionRow& row : in)
    {
        if (row.actorId.empty() || row.delta == 0)
            continue;
        out.push_back(row);
    }
    return out;
}

nlohmann::json eventStatusToJson(
    float health,
    float energy,
    float resolve,
    float lucidity,
    float charisma,
    float money,
    const std::string& key,
    const std::string& onZeroLucidity,
    const std::string& tabActor,
    float tabDelta,
    const std::vector<EventOpinionRow>& opinions)
{
    nlohmann::json st = nlohmann::json::object();
    setOrEraseFloat(st, "health", health);
    setOrEraseFloat(st, "energy", energy);
    setOrEraseFloat(st, "resolve", resolve);
    setOrEraseFloat(st, "lucidity", lucidity);
    setOrEraseFloat(st, "charisma", charisma);
    setOrEraseFloat(st, "money", money);
    setOrEraseString(st, "key", key);
    setOrEraseString(st, "onZeroLucidity", onZeroLucidity);

    const bool tabSet = !tabActor.empty() && tabDelta != 0.0f;
    if (tabSet)
    {
        st["actor"] = tabActor;
        st["actorTab"] = tabDelta;
    }

    const std::vector<EventOpinionRow> rows = cleanedOpinions(opinions);
    if (!rows.empty())
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const EventOpinionRow& row : rows)
        {
            arr.push_back(nlohmann::json{
                {"actor", row.actorId},
                {"delta", row.delta}});
        }
        st["opinions"] = arr;
    }

    if (rows.size() == 1)
    {
        const EventOpinionRow& row = rows.front();
        if (!tabSet)
        {
            st["actor"] = row.actorId;
            st["actorOpinion"] = row.delta;
        }
        else if (tabActor == row.actorId)
            st["actorOpinion"] = row.delta;
    }

    return st;
}

} // namespace

void fillEventNodeFromChoiceJson(DialogFlowNode& node, const nlohmann::json& choice)
{
    if (!choice.is_object())
        return;
    if (choice.contains("grantStoryFlag") && choice["grantStoryFlag"].is_string())
        node.eventId = choice["grantStoryFlag"].get<std::string>();

    const nlohmann::json& st = choice.contains("status") && choice["status"].is_object()
        ? choice["status"]
        : nlohmann::json::object();
    if (!st.is_object())
        return;

    node.eventStatusHealth = st.value("health", 0.0f);
    node.eventStatusEnergy = st.value("energy", 0.0f);
    node.eventStatusResolve = st.value("resolve", st.value("tenacity", 0.0f));
    node.eventStatusLucidity = st.value("lucidity", 0.0f);
    node.eventStatusCharisma = st.value("charisma", 0.0f);
    node.eventStatusMoney = st.value("money", 0.0f);
    node.eventStatusKey = st.value("key", std::string());
    node.eventOnZeroLucidity = st.value("onZeroLucidity", std::string());

    const std::string actor = readActorField(st);
    node.eventTabDelta = st.value("actorTab", 0.0f);
    node.eventTabActor = (node.eventTabDelta != 0.0f) ? actor : std::string();

    node.eventOpinions.clear();
    if (st.contains("opinions") && st["opinions"].is_array())
    {
        for (const nlohmann::json& row : st["opinions"])
        {
            if (!row.is_object())
                continue;
            EventOpinionRow r;
            r.actorId = row.value("actor", std::string());
            r.delta = row.value("delta", row.value("actorOpinion", 0));
            if (r.actorId.empty() && r.delta == 0)
                continue;
            node.eventOpinions.push_back(r);
        }
    }
    else
    {
        const int opinion = st.value("actorOpinion", 0);
        if (opinion != 0 && !actor.empty())
            node.eventOpinions.push_back(EventOpinionRow{actor, opinion});
    }
}

void EventAuthoringDialog::resetWorkingState()
{
    loadedJsonPointer.clear();
    flagId.clear();
    statusHealth = 0.0f;
    statusEnergy = 0.0f;
    statusResolve = 0.0f;
    statusLucidity = 0.0f;
    statusCharisma = 0.0f;
    statusMoney = 0.0f;
    statusKey.clear();
    statusOnZeroLucidity.clear();
    tabActor.clear();
    tabDelta = 0.0f;
    opinions.clear();
    focusField = kFocusFlag;
    focusDraft.clear();
    scrollY = 0.0f;
    lastContentH = 0.0f;
    lastScrollClip = {};
    status.clear();
    error.clear();
}

void EventAuthoringDialog::copyNodeToDialog(const DialogFlowNode& n)
{
    flagId = n.eventId;
    statusHealth = n.eventStatusHealth;
    statusEnergy = n.eventStatusEnergy;
    statusResolve = n.eventStatusResolve;
    statusLucidity = n.eventStatusLucidity;
    statusCharisma = n.eventStatusCharisma;
    statusMoney = n.eventStatusMoney;
    statusKey = n.eventStatusKey;
    statusOnZeroLucidity = n.eventOnZeroLucidity;
    tabActor = n.eventTabActor;
    tabDelta = n.eventTabDelta;
    opinions = n.eventOpinions;
}

void EventAuthoringDialog::copyDialogToNode(DialogFlowNode& n) const
{
    n.eventId = flagId;
    n.eventStatusHealth = statusHealth;
    n.eventStatusEnergy = statusEnergy;
    n.eventStatusResolve = statusResolve;
    n.eventStatusLucidity = statusLucidity;
    n.eventStatusCharisma = statusCharisma;
    n.eventStatusMoney = statusMoney;
    n.eventStatusKey = statusKey;
    n.eventOnZeroLucidity = statusOnZeroLucidity;
    n.eventTabActor = tabActor;
    n.eventTabDelta = tabDelta;
    n.eventOpinions = opinions;
}

nlohmann::json* EventAuthoringDialog::resolveChoiceObject()
{
    return const_cast<nlohmann::json*>(
        static_cast<const EventAuthoringDialog*>(this)->resolveChoiceObject());
}

const nlohmann::json* EventAuthoringDialog::resolveChoiceObject() const
{
    if (docs == nullptr)
        return nullptr;
    const std::string pointer =
        loadedJsonPointer.empty() && node != nullptr ? node->jsonPointer : loadedJsonPointer;
    if (pointer.empty())
        return nullptr;
    const std::string choicePtr = choicePointerFrom(pointer);
    const nlohmann::json* obj = docs->conversationJsonAt(choicePtr);
    if (obj != nullptr && obj->is_object())
        return obj;
    return nullptr;
}

void EventAuthoringDialog::loadFromNodeAndJson()
{
    resetWorkingState();
    if (node != nullptr)
        copyNodeToDialog(*node);

    loadedJsonPointer = node != nullptr ? node->jsonPointer : std::string{};

    nlohmann::json* obj = resolveChoiceObject();
    if (obj != nullptr)
    {
        DialogFlowNode tmp;
        if (node != nullptr)
            tmp = *node;
        fillEventNodeFromChoiceJson(tmp, *obj);
        copyNodeToDialog(tmp);
    }
}

void EventAuthoringDialog::openForEvent(const std::string& inSceneId, DialogFlowNode* inNode)
{
    if (inNode == nullptr || inNode->kind != DialogNodeKind::TriggerEvent)
        return;
    sceneId = inSceneId;
    node = inNode;
    loadFromNodeAndJson();
    open = true;
    ignoreInputFrames = 2;
    waitMouseRelease = true;
}

void EventAuthoringDialog::closeDialog()
{
    open = false;
    waitMouseRelease = false;
    node = nullptr;
}

void EventAuthoringDialog::addOpinionRow()
{
    if (static_cast<int>(opinions.size()) >= kMaxOpinions)
    {
        error = "At most 16 reputation rows.";
        return;
    }
    opinions.push_back(EventOpinionRow{});
    waitMouseRelease = true;
    focusField = kFocusOpinionBase + static_cast<int>(opinions.size() - 1) * 2;
    status = "Reputation row added.";
    error.clear();
}

void EventAuthoringDialog::removeOpinionRow(int index)
{
    if (index < 0 || index >= static_cast<int>(opinions.size()))
        return;
    opinions.erase(opinions.begin() + index);
    const int row = opinionRowFromField(focusField);
    if (row == index || row >= static_cast<int>(opinions.size()))
    {
        focusField = kFocusFlag;
        focusDraft.clear();
    }
    waitMouseRelease = true;
    status = "Reputation row removed.";
    error.clear();
}

bool EventAuthoringDialog::applyChanges()
{
    commitNumericFocus();
    if (node == nullptr)
    {
        error = "No Trigger Event node bound.";
        return false;
    }
    flagId = sanitizeFlagId(flagId);
    if (flagId.empty())
    {
        error = "Set a story flag id.";
        return false;
    }
    copyDialogToNode(*node);

    if (docs == nullptr)
    {
        error = "Document not wired.";
        return false;
    }
    if (!docs->conversationsLoaded)
        docs->loadConversationsDocument();
    if (!docs->conversationsRoot.is_object())
        docs->conversationsRoot = nlohmann::json::object();

    nlohmann::json* obj = resolveChoiceObject();
    if (obj == nullptr)
    {
        docs->markDirty();
        status = "Saved editor-local (no JSON pointer yet).";
        error.clear();
        if (onSaved)
            onSaved();
        return true;
    }

    (*obj)["grantStoryFlag"] = flagId;
    nlohmann::json st = eventStatusToJson(
        statusHealth,
        statusEnergy,
        statusResolve,
        statusLucidity,
        statusCharisma,
        statusMoney,
        statusKey,
        statusOnZeroLucidity,
        tabActor,
        tabDelta,
        opinions);
    if (st.empty())
        obj->erase("status");
    else
        (*obj)["status"] = st;

    docs->markDirty();
    status = "Saved Trigger Event " + flagId;
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

bool EventAuthoringDialog::focusedIsNumeric() const
{
    if (focusField >= kFocusHealth && focusField <= kFocusMoney)
        return true;
    if (focusField == kFocusTabDelta)
        return true;
    return fieldIsOpinionDelta(focusField);
}

void EventAuthoringDialog::beginNumericFocus(int field, const std::string& initial)
{
    commitNumericFocus();
    focusField = field;
    focusDraft = initial;
}

void EventAuthoringDialog::commitNumericFocus()
{
    if (!focusedIsNumeric())
        return;
    const char* s = focusDraft.c_str();
    auto asInt = [&]() { return static_cast<int>(std::strtol(s, nullptr, 10)); };
    auto asFloat = [&]() { return std::strtof(s, nullptr); };
    switch (focusField)
    {
    case kFocusHealth:
        statusHealth = asFloat();
        break;
    case kFocusEnergy:
        statusEnergy = asFloat();
        break;
    case kFocusResolve:
        statusResolve = asFloat();
        break;
    case kFocusLucidity:
        statusLucidity = asFloat();
        break;
    case kFocusCharisma:
        statusCharisma = asFloat();
        break;
    case kFocusMoney:
        statusMoney = asFloat();
        break;
    case kFocusTabDelta:
        tabDelta = asFloat();
        break;
    default:
        if (fieldIsOpinionDelta(focusField))
        {
            const int row = opinionRowFromField(focusField);
            if (row >= 0 && row < static_cast<int>(opinions.size()))
                opinions[static_cast<size_t>(row)].delta = asInt();
        }
        break;
    }
    focusDraft.clear();
}

std::string* EventAuthoringDialog::focusedString()
{
    switch (focusField)
    {
    case kFocusFlag:
        return &flagId;
    case kFocusKey:
        return &statusKey;
    case kFocusOnZero:
        return &statusOnZeroLucidity;
    case kFocusTabActor:
        return &tabActor;
    default:
        break;
    }
    if (fieldIsOpinionActor(focusField))
    {
        const int row = opinionRowFromField(focusField);
        if (row >= 0 && row < static_cast<int>(opinions.size()))
            return &opinions[static_cast<size_t>(row)].actorId;
    }
    return nullptr;
}

void EventAuthoringDialog::typeIntoFocusedField()
{
    if (focusedIsNumeric())
    {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        {
            commitNumericFocus();
            return;
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        {
            if (!focusDraft.empty())
                focusDraft.pop_back();
            return;
        }
        const bool allowDot = (focusField >= kFocusHealth && focusField <= kFocusMoney)
            || focusField == kFocusTabDelta;
        int ch = GetCharPressed();
        while (ch > 0)
        {
            if (ch >= '0' && ch <= '9')
                focusDraft.push_back(static_cast<char>(ch));
            else if (allowDot && ch == '.' && focusDraft.find('.') == std::string::npos)
                focusDraft.push_back('.');
            else if (ch == '-' && focusDraft.empty())
                focusDraft.push_back('-');
            ch = GetCharPressed();
        }
        return;
    }

    std::string* buffer = focusedString();
    if (buffer == nullptr)
        return;
    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp >= 32)
            insertUtf8(*buffer, cp);
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspaceUtf8(*buffer);
}

void EventAuthoringDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }
    if (waitMouseRelease)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        closeDialog();
        return;
    }
    {
        const Vector2 mouse = GetMousePosition();
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && lastScrollClip.width > 1.0f
            && CheckCollisionPointRec(mouse, lastScrollClip))
        {
            const float maxScroll =
                std::max(0.0f, lastContentH - lastScrollClip.height);
            scrollY = std::clamp(scrollY - wheel * 28.0f, 0.0f, maxScroll);
        }
    }
    typeIntoFocusedField();
}

void EventAuthoringDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0
        && editorMousePressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);
    const float dialogW = std::min(640.0f, screenW - 32.0f);
    const float dialogH = std::min(640.0f, screenH - 24.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f, (screenH - dialogH) * 0.5f, dialogW, dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    const float pad = 16.0f;
    const float fieldX = dialog.x + pad;
    const float fieldW = dialog.width - pad * 2.0f;
    float y = dialog.y + pad;

    DrawTextEx(bold, "Edit Event", {fieldX, y}, kFontHeading, 1.0f, kTextPrimary);
    y += 28.0f;
    DrawTextEx(
        font,
        sceneId.empty() ? "(no scene)" : sceneId.c_str(),
        {fieldX, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 18.0f;

    const float footerH = 52.0f;
    const Rectangle scrollClip = {
        fieldX, y, fieldW, dialog.y + dialogH - footerH - y};
    lastScrollClip = scrollClip;

    float contentY = 0.0f;
    auto advance = [&](float dy) { contentY += dy; };
    auto rowY = [&]() { return scrollClip.y + contentY - scrollY; };

    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);

    auto reenterScroll = [&]() {
        BeginScissorMode(
            (int)scrollClip.x,
            (int)scrollClip.y,
            (int)scrollClip.width,
            (int)scrollClip.height);
    };

    auto stringField =
        [&](int fieldId, const char* label, std::string& value, const char* placeholder) {
            DrawTextEx(font, label, {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
            advance(16.0f);
            Rectangle field = {fieldX, rowY(), fieldW, 28.0f};
            DrawRectangleRec(field, Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                field,
                focusField == fieldId ? 2.0f : 1.0f,
                focusField == fieldId ? kPanelBorder : kPanelInnerEdge);
            drawClippedFieldText(
                font,
                field,
                value,
                placeholder,
                kFontSmall,
                value.empty() ? kTextMuted : kTextPrimary);
            reenterScroll();
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, field))
            {
                commitNumericFocus();
                focusField = fieldId;
            }
            advance(36.0f);
        };

    auto numericField =
        [&](int fieldId, const char* label, const std::string& shown, float width = 80.0f) {
            DrawTextEx(
                font, label, {fieldX, rowY() + 4.0f}, kFontSmall, 1.0f, kTextPrimary);
            Rectangle field = {fieldX + fieldW - width, rowY(), width, 26.0f};
            const bool focused = focusField == fieldId;
            DrawRectangleRec(
                field, focused ? Color{50, 48, 62, 255} : Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                field, focused ? 2.0f : 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
            const std::string text = focused
                ? (focusDraft.empty() ? "|" : focusDraft)
                : shown;
            DrawTextEx(
                font,
                text.c_str(),
                {field.x + 6.0f, field.y + 4.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, field))
                beginNumericFocus(fieldId, shown);
            advance(30.0f);
        };

    stringField(kFocusFlag, "Story flag id", flagId, "(e.g. saloon_interior:chose_usual)");

    DrawTextEx(bold, "Player status", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    DrawTextEx(
        font,
        "Signed deltas applied when the parent choice/line fires.",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);

    {
        const char* labels[] = {"H", "E", "R", "L", "C"};
        const int fields[] = {
            kFocusHealth, kFocusEnergy, kFocusResolve, kFocusLucidity, kFocusCharisma};
        const float values[] = {
            statusHealth, statusEnergy, statusResolve, statusLucidity, statusCharisma};
        const float gap = 8.0f;
        const float colW = (fieldW - gap * 4.0f) / 5.0f;
        const float cellY = rowY();
        for (int i = 0; i < 5; ++i)
        {
            const float cx = fieldX + static_cast<float>(i) * (colW + gap);
            DrawTextEx(
                font, labels[i], {cx, cellY}, kFontTiny, 1.0f, kTextMuted);
            Rectangle field = {cx, cellY + 16.0f, colW, 26.0f};
            const bool focused = focusField == fields[i];
            DrawRectangleRec(
                field, focused ? Color{50, 48, 62, 255} : Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                field, focused ? 2.0f : 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
            const std::string shown = formatFloat(values[i]);
            const std::string text = focused
                ? (focusDraft.empty() ? "|" : focusDraft)
                : shown;
            DrawTextEx(
                font,
                text.c_str(),
                {field.x + 6.0f, field.y + 4.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, field))
                beginNumericFocus(fields[i], shown);
        }
        advance(48.0f);
    }

    numericField(kFocusMoney, "Money", formatFloat(statusMoney));
    stringField(kFocusKey, "Once key", statusKey, "(status.key — skip if already consumed)");
    stringField(
        kFocusOnZero, "On zero lucidity", statusOnZeroLucidity, "(e.g. restart_cabin)");

    DrawTextEx(bold, "Actor tab", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    stringField(kFocusTabActor, "Tab actor", tabActor, "(e.g. bartender)");
    numericField(kFocusTabDelta, "Tab delta", formatFloat(tabDelta));

    DrawTextEx(bold, "Actor reputation", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    {
        const float plusW = 28.0f;
        const float headerY = rowY();
        Rectangle plusBtn = {fieldX + fieldW - plusW, headerY - 2.0f, plusW, 24.0f};
        drawEditorButton(font, plusBtn, "+", true, true);
        if (canClick && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, plusBtn))
            addOpinionRow();
    }
    advance(22.0f);
    DrawTextEx(
        font,
        "One row per actor. Dual-writes status.opinions[] (legacy actorOpinion if one).",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);

    if (opinions.empty())
    {
        DrawTextEx(
            font,
            "(none — click + to add)",
            {fieldX, rowY()},
            kFontSmall,
            1.0f,
            kTextMuted);
        advance(22.0f);
    }
    else
    {
        const float xW = 22.0f;
        const float deltaW = 64.0f;
        for (int i = 0; i < static_cast<int>(opinions.size()); ++i)
        {
            EventOpinionRow& row = opinions[static_cast<size_t>(i)];
            const Color swatch = dialogActorResponsePortColor(i);
            const float cy = rowY() + 14.0f;
            DrawCircleV({fieldX + 7.0f, cy}, 6.0f, swatch);
            DrawCircleLines(
                (int)(fieldX + 7.0f), (int)cy, 6.0f, Color{30, 28, 36, 255});

            const int actorField = kFocusOpinionBase + i * 2;
            const int deltaField = actorField + 1;
            const float actorX = fieldX + 18.0f;
            const float actorW = fieldW - 18.0f - deltaW - xW - 16.0f;
            Rectangle actorRect = {actorX, rowY(), actorW, 26.0f};
            DrawRectangleRec(actorRect, Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                actorRect,
                focusField == actorField ? 2.0f : 1.0f,
                focusField == actorField ? kPanelBorder : kPanelInnerEdge);
            drawClippedFieldText(
                font,
                actorRect,
                row.actorId,
                "(actor id)",
                kFontSmall,
                row.actorId.empty() ? kTextMuted : kTextPrimary);
            reenterScroll();
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, actorRect))
            {
                commitNumericFocus();
                focusField = actorField;
            }

            Rectangle deltaRect = {
                actorX + actorW + 8.0f, rowY(), deltaW, 26.0f};
            const bool deltaFocused = focusField == deltaField;
            DrawRectangleRec(
                deltaRect,
                deltaFocused ? Color{50, 48, 62, 255} : Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                deltaRect,
                deltaFocused ? 2.0f : 1.0f,
                deltaFocused ? kPanelBorder : kPanelInnerEdge);
            const std::string shown = formatSignedInt(row.delta);
            const std::string text = deltaFocused
                ? (focusDraft.empty() ? "|" : focusDraft)
                : shown;
            DrawTextEx(
                font,
                text.c_str(),
                {deltaRect.x + 6.0f, deltaRect.y + 4.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, deltaRect))
                beginNumericFocus(deltaField, shown);

            Rectangle xRect = {fieldX + fieldW - xW, rowY() + 2.0f, xW, 22.0f};
            DrawTextEx(
                bold,
                "X",
                {xRect.x + 4.0f, xRect.y + 1.0f},
                kFontSmall,
                1.0f,
                Color{210, 90, 90, 255});
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, xRect))
            {
                removeOpinionRow(i);
                break;
            }
            advance(30.0f);
        }
    }

    lastContentH = contentY;
    const float maxScroll = std::max(0.0f, lastContentH - scrollClip.height);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    EndScissorMode();

    if (maxScroll > 1.0f)
    {
        const float trackX = dialog.x + dialog.width - 10.0f;
        const Rectangle track = {trackX, scrollClip.y, 4.0f, scrollClip.height};
        DrawRectangleRec(track, Color{40, 36, 48, 255});
        const float thumbH = std::max(
            24.0f, scrollClip.height * (scrollClip.height / lastContentH));
        const float thumbY =
            scrollClip.y + (scrollClip.height - thumbH) * (scrollY / maxScroll);
        DrawRectangleRec({trackX, thumbY, 4.0f, thumbH}, kPanelAccent);
    }

    const float btnY = dialog.y + dialogH - 44.0f;
    Rectangle saveBtn = {dialog.x + pad, btnY, 110.0f, 32.0f};
    Rectangle cancelBtn = {saveBtn.x + 120.0f, btnY, 110.0f, 32.0f};
    drawEditorButton(font, saveBtn, "Apply", true, true);
    drawEditorButton(font, cancelBtn, "Close", false, true);
    if (canClick && CheckCollisionPointRec(mouse, saveBtn))
        applyChanges();
    if (canClick && CheckCollisionPointRec(mouse, cancelBtn))
        closeDialog();

    const float msgX = cancelBtn.x + 120.0f;
    const float msgMaxW = dialog.x + dialog.width - pad - msgX;
    if (!error.empty())
    {
        const std::string shown = ellipsizeUi(font, error, kFontTiny, msgMaxW);
        DrawTextEx(
            font, shown.c_str(), {msgX, btnY + 8.0f}, kFontTiny, 1.0f, Color{220, 90, 80, 255});
    }
    else if (!status.empty())
    {
        const std::string shown = ellipsizeUi(font, status, kFontTiny, msgMaxW);
        DrawTextEx(
            font, shown.c_str(), {msgX, btnY + 8.0f}, kFontTiny, 1.0f, Color{120, 180, 120, 255});
    }

    if (canClick && !CheckCollisionPointRec(mouse, dialog))
        closeDialog();
}

} // namespace timberline_editor
