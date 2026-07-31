/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_THEME_H
#define TIMBERLINE_EDITOR_THEME_H
#include <raylib.h>

namespace timberline_editor
{

const Color kPanelFill = {28, 26, 34, 255};
const Color kPanelBorder = {168, 138, 72, 255};
const Color kPanelAccent = {96, 78, 48, 255};
const Color kPanelInnerEdge = {48, 42, 54, 255};
const Color kDividerTrack = {16, 14, 20, 255};
const Color kDividerGrip = {110, 92, 52, 255};
const Color kDividerGripActive = {188, 158, 88, 255};
const Color kTextPrimary = {220, 212, 196, 255};
const Color kTextMuted = {132, 122, 104, 255};
const Color kCanvasBg = {18, 17, 22, 255};
const Color kSelection = {120, 96, 48, 180};
const Color kExitArrow = {168, 138, 72, 220};
const Color kButtonDisabled = {48, 46, 54, 255};
const Color kTextDisabled = {90, 86, 96, 255};
const Color kModalOverlay = {0, 0, 0, 160};
const Color kModalFill = {32, 30, 40, 255};
const float kDividerSize = 8.0f;
const float kDividerHitPadding = 6.0f;
// extra grab area beyond the visible grip

const float kPanelRoundness = 0.03f;
const float kPanelBorderThick = 2.0f;
const float kStatusBarHeight = 22.0f;
const float kTopAreaRatio = 2.0f / 3.0f;
const float kLeftPaneRatio = 0.4f;
// was 0.2; doubled so scene labels stay readable

const float kMinLeftWidth = 320.0f;
const float kMinMainWidth = 280.0f;
const float kMinTopHeight = 200.0f;
const float kMinBottomHeight = 140.0f;
const float kTabHeight = 34.0f;
const float kSceneCardWidth = 160.0f;
const float kSceneCardMinHeight = 110.0f;
const float kSceneCardThumbHeight = 68.0f;
const float kSceneCardTitleFont = 14.0f;
// was 12

const float kSceneCardTitleLineHeight = 17.0f;
const int kSceneCardMaxTitleLines = 4;
// Corridors between cards for mid-route turns (endpoints stay flush with card edges).

const float kLayoutGapX = 96.0f;
const float kLayoutGapY = 96.0f;
// UI body fonts are ~2pt larger than the original defaults.

const float kFontTiny = 14.0f;
const float kFontSmall = 15.0f;
const float kFontBody = 16.0f;
const float kFontLabel = 17.0f;
const float kFontTitle = 18.0f;
const float kFontHeading = 20.0f;
// How far into the corridor the perpendicular exit/enter stubs travel before turning.

const float kLinkStubLength = 28.0f;
const float kArrowHeadLength = 12.0f;
const float kArrowHeadHalfWidth = 5.0f;
const float kLinkEndCapRadius = 7.0f;
const float kWireHopRadius = 8.0f;
// schematic-style jump at wire crossings

const float kLayoutOriginX = 40.0f;
const float kLayoutOriginY = 48.0f;
const float kListThumbSize = 96.0f;
// doubled for the left scene browser

const float kListRowHeight = 108.0f;
const float kListNameFont = 18.0f;
// +2 from prior list body size

const float kListMetaFont = 16.0f;
const float kListTabFont = 16.0f;
const float kCanvasChromeHeight = 36.0f;
const float kScrollBarSize = 14.0f;
const float kScrollContentPad = 48.0f;
const Color kScrollTrack = {22, 20, 28, 255};
const Color kScrollThumb = {110, 92, 52, 255};
const Color kScrollThumbActive = {168, 138, 72, 255};

// Key navigation hold-to-repeat in the text popup (arrows, backspace, delete).
// Initial delay must be long enough that a quick tap only moves once; 0.07s
// (game click-hold) was too short and often double-fired. Repeat cadence is
// separate and can stay snappy once the key is held.
const float kKeyRepeatInitialDelaySeconds = 0.28f;
const float kKeyRepeatEverySeconds = 0.04f;

} // namespace timberline_editor

#endif
