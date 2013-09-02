/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.
    Copyright (C) 1995 Ronny Wester
    Copyright (C) 2003 Jeremy Chin
    Copyright (C) 2003-2007 Lucas Martin-King

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    This file incorporates work covered by the following copyright and
    permission notice:

    Copyright (c) 2013, Cong Xu
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/
#include "hud.h"

#include <time.h>

#include "actors.h"
#include "automap.h"
#include "drawtools.h"
#include "game.h"
#include "mission.h"
#include "pic_manager.h"
#include "text.h"

void FPSCounterInit(FPSCounter *counter)
{
	counter->elapsed = 0;
	counter->framesDrawn = 0;
	counter->fps = 0;
}
void FPSCounterUpdate(FPSCounter *counter, int ms)
{
	counter->elapsed += ms;
	if (counter->elapsed > 1000)
	{
		counter->fps = counter->framesDrawn;
		counter->framesDrawn = 0;
		counter->elapsed -= 1000;
	}
}
void FPSCounterDraw(FPSCounter *counter)
{
	char s[50];
	counter->framesDrawn++;
	sprintf(s, "FPS: %d", counter->fps);
	CDogsTextStringSpecial(s, TEXT_RIGHT | TEXT_BOTTOM, 10, 5 + CDogsTextHeight());
}

void WallClockSetTime(WallClock *wc)
{
	time_t t = time(NULL);
	struct tm *tp = localtime(&t);
	wc->hours = tp->tm_hour;
	wc->minutes = tp->tm_min;
}
void WallClockInit(WallClock *wc)
{
	wc->elapsed = 0;
	WallClockSetTime(wc);
}
void WallClockUpdate(WallClock *wc, int ms)
{
	wc->elapsed += ms;
	if (wc->elapsed > 60*1000)	// update every minute
	{
		WallClockSetTime(wc);
		wc->elapsed -= 60*1000;
	}
}
void WallClockDraw(WallClock *wc)
{
	char s[50];
	sprintf(s, "%02d:%02d", wc->hours, wc->minutes);
	CDogsTextStringSpecial(s, TEXT_LEFT | TEXT_BOTTOM, 10, 5 + CDogsTextHeight());
}

void HUDInit(
	HUD *hud,
	InterfaceConfig *config,
	GraphicsDevice *device,
	struct MissionOptions *mission)
{
	hud->mission = mission;
	strcpy(hud->message, "");
	hud->messageTicks = 0;
	hud->config = config;
	hud->device = device;
	FPSCounterInit(&hud->fpsCounter);
	WallClockInit(&hud->clock);
}

void HUDDisplayMessage(HUD *hud, const char *msg)
{
	strcpy(hud->message, msg);
	hud->messageTicks = 140;
}

void HUDUpdate(HUD *hud, int ms)
{
	hud->messageTicks -= ms;
	if (hud->messageTicks < 0)
	{
		hud->messageTicks = 0;
	}
	FPSCounterUpdate(&hud->fpsCounter, ms);
	WallClockUpdate(&hud->clock, ms);
}


// Draw a gauge with an outer background and inner level
// +--------------+
// |XXXXXXXX|     |
// +--------------+
static void DrawGauge(
	GraphicsDevice *device,
	Vec2i pos, Vec2i size, int innerWidth,
	color_t barColor, color_t backColor,
	int textFlags)
{
	Vec2i barPos = Vec2iAdd(pos, Vec2iNew(1, 1));
	Vec2i barSize = Vec2iNew(MAX(0, innerWidth - 2), size.y - 2);
	if (textFlags & TEXT_RIGHT)
	{
		pos.x = device->cachedConfig.ResolutionWidth - pos.x - size.x;
		barPos.x = device->cachedConfig.ResolutionWidth - barPos.x - barSize.x;
	}
	DrawRectangleRGB(device, pos, size, backColor, DRAW_FLAG_ROUNDED);
	DrawRectangleRGB(device, barPos, barSize, barColor, 0);
}

static void DrawWeaponStatus(
	GraphicsDevice *device, const Weapon *weapon, Vec2i pos, int textFlags)
{
	// don't draw gauge if not reloading
	if (weapon->lock > 0)
	{
		Vec2i gaugePos = Vec2iAdd(pos, Vec2iNew(-1, -1));
		Vec2i size = Vec2iNew(50, CDogsTextHeight() + 1);
		color_t barColor = { 0, 0, 255, 255 };
		int maxLock = gGunDescriptions[weapon->gun].Lock;
		int innerWidth;
		color_t backColor = { 128, 128, 128, 255 };
		if (maxLock == 0)
		{
			innerWidth = 0;
		}
		else
		{
			innerWidth = MAX(1, size.x * (maxLock - weapon->lock) / maxLock);
		}
		DrawGauge(
			device, gaugePos, size, innerWidth, barColor, backColor, textFlags);
	}
	CDogsTextStringSpecial(GunGetName(weapon->gun), textFlags, pos.x, pos.y);
}

static void DrawHealth(
	GraphicsDevice *device, TActor *actor, Vec2i pos, int textFlags)
{
	char s[50];
	Vec2i gaugePos = Vec2iAdd(pos, Vec2iNew(-1, -1));
	Vec2i size = Vec2iNew(50, CDogsTextHeight() + 1);
	HSV hsv = { 0.0, 1.0, 1.0 };
	color_t barColor;
	int health = actor->health;
	int maxHealth = gCharacterDesc[actor->character].maxHealth;
	int innerWidth;
	color_t backColor = { 50, 0, 0, 255 };
	innerWidth = MAX(1, size.x * health / maxHealth);
	if (actor->poisoned)
	{
		hsv.h = 120.0;
		hsv.v = 0.5;
	}
	else
	{
		double maxHealthHue = 50.0;
		double minHealthHue = 0.0;
		hsv.h =
			((maxHealthHue - minHealthHue) * health / maxHealth + minHealthHue);
	}
	barColor = ColorTint(colorWhite, hsv);
	DrawGauge(
		device, gaugePos, size, innerWidth, barColor, backColor, textFlags);
	sprintf(s, "%d", health);
	CDogsTextStringSpecial(s, textFlags, pos.x, pos.y);
}

#define HUDFLAGS_PLACE_RIGHT	0x01
#define HUDFLAGS_HALF_SCREEN	0x02
#define HUDFLAGS_SHARE_SCREEN	0x04

#define AUTOMAP_PADDING	5
#define AUTOMAP_SIZE	45
static void DrawRadar(GraphicsDevice *device, TActor *p, int scale, int flags)
{
	Vec2i automapSize = Vec2iNew(AUTOMAP_SIZE, AUTOMAP_SIZE);
	Vec2i pos = Vec2iZero();
	// Five possible map positions:
	// top-right (player 1 only)
	// top-left (player 2 only)
	// top-left-of-middle (player 1 when two players)
	// top-right-of-middle (player 2 when two players)
	// top (two player shared screen)
	if (!(flags & HUDFLAGS_PLACE_RIGHT) &&
		!(flags & HUDFLAGS_HALF_SCREEN))
	{
		// player 1 only
		pos = Vec2iNew(
			device->cachedConfig.ResolutionWidth - AUTOMAP_SIZE - AUTOMAP_PADDING,
			AUTOMAP_PADDING);
	}
	else if (
		(flags & HUDFLAGS_PLACE_RIGHT) &&
		!(flags & HUDFLAGS_HALF_SCREEN))
	{
		// player 2 only
		pos = Vec2iNew(AUTOMAP_PADDING, AUTOMAP_PADDING);
	}
	else if (
		!(flags & HUDFLAGS_PLACE_RIGHT) &&
		(flags & HUDFLAGS_HALF_SCREEN))
	{
		// player 1 when two players
		pos = Vec2iNew(
			device->cachedConfig.ResolutionWidth / 2 - AUTOMAP_SIZE - AUTOMAP_PADDING,
			AUTOMAP_PADDING);
	}
	else if (
		(flags & HUDFLAGS_PLACE_RIGHT) &&
		(flags & HUDFLAGS_HALF_SCREEN))
	{
		// player 2 when two players
		pos = Vec2iNew(
			device->cachedConfig.ResolutionWidth / 2 + AUTOMAP_PADDING,
			AUTOMAP_PADDING);
	}
	else if (flags & HUDFLAGS_SHARE_SCREEN)
	{
		// share screen
		pos = Vec2iNew(
			device->cachedConfig.ResolutionWidth / 2 - automapSize.x / 2,
			AUTOMAP_PADDING);
	}

	if (!Vec2iEqual(pos, Vec2iZero()))
	{
		AutomapDrawRegion(
			gMap,
			pos,
			automapSize,
			p,
			scale,
			AUTOMAP_FLAGS_MASK);
	}
}

// Draw player's score, health etc.
static void DrawPlayerStatus(
	GraphicsDevice *device, struct PlayerData *data, TActor *p, int flags)
{
	char s[50];
	int textFlags = TEXT_TOP | TEXT_LEFT;
	if (flags & HUDFLAGS_PLACE_RIGHT)
	{
		textFlags |= TEXT_RIGHT;
	}

	CDogsTextStringSpecial(data->name, textFlags, 5, 5);
	if (IsScoreNeeded(gCampaign.Entry.mode))
	{
		sprintf(s, "Score: %d", data->score);
	}
	else
	{
		s[0] = 0;
	}
	if (p)
	{
		Vec2i pos = Vec2iNew(5, 5 + 1 + CDogsTextHeight());
		const int rowHeight = 1 + CDogsTextHeight();
		DrawWeaponStatus(device, &p->weapon, pos, textFlags);
		pos.y += rowHeight;
		CDogsTextStringSpecial(s, textFlags, pos.x, pos.y);
		pos.y += rowHeight;
		DrawHealth(device, p, pos, textFlags);
	}
	else
	{
		CDogsTextStringSpecial(s, textFlags, 5, 5 + 1 * CDogsTextHeight());
	}

	if (gConfig.Interface.ShowHUDMap)
	{
		DrawRadar(device, p, 1, flags);
	}
}

static void DrawKeycard(int x, int y, const TOffsetPic * pic)
{
	DrawTPic(
		x + pic->dx, y + pic->dy,
		PicManagerGetOldPic(&gPicManager, pic->picIndex));
}

void DrawKeycards(HUD *hud)
{
	int keyFlags[] =
	{
		FLAGS_KEYCARD_YELLOW,
		FLAGS_KEYCARD_GREEN,
		FLAGS_KEYCARD_BLUE,
		FLAGS_KEYCARD_RED
	};
	int i;
	int xOffset = -30;
	int xOffsetIncr = 20;
	int yOffset = 20;
	for (i = 0; i < 4; i++)
	{
		if (hud->mission->flags & keyFlags[i])
		{
			DrawKeycard(
				CenterX(cGeneralPics[hud->mission->keyPics[i]].dx) - xOffset,
				yOffset,
				&cGeneralPics[hud->mission->keyPics[i]]);
		}
		xOffset += xOffsetIncr;
	}
}

void HUDDraw(HUD *hud, int isPaused, int isEscExit)
{
	char s[50];
	static time_t ot = -1;
	static time_t t = 0;
	static time_t td = 0;
	int flags = 0;
	if (gOptions.twoPlayers && gPlayer1 && gPlayer2)
	{
		flags |= HUDFLAGS_HALF_SCREEN;
	}

	DrawPlayerStatus(hud->device, &gPlayer1Data, gPlayer1, flags);
	if (gOptions.twoPlayers)
	{
		DrawPlayerStatus(
			hud->device,
			&gPlayer2Data,
			gPlayer2,
			flags | HUDFLAGS_PLACE_RIGHT);
	}

	if (!gPlayer1 && !gPlayer2)
	{
		if (gCampaign.Entry.mode != CAMPAIGN_MODE_DOGFIGHT)
		{
			CDogsTextStringAtCenter("Game Over!");
		}
		else
		{
			CDogsTextStringAtCenter("Double Kill!");
		}
	}
	else if (IsMissionComplete(hud->mission))
	{
		sprintf(s, "Pickup in %d seconds\n",
			(gMission.pickupTime + 69) / 70);
		CDogsTextStringAtCenter(s);
	}

	if (isPaused)
	{
		if (isEscExit)
		{
			CDogsTextStringAtCenter("Press Esc again to quit");
		}
		else
		{
			CDogsTextStringAtCenter("Paused");
		}
	}

	if (hud->messageTicks > 0)
	{
		CDogsTextStringSpecial(hud->message, TEXT_XCENTER | TEXT_TOP, 0, 20);
	}

	if (hud->config->ShowFPS)
	{
		FPSCounterDraw(&hud->fpsCounter);
	}
	if (hud->config->ShowTime)
	{
		WallClockDraw(&hud->clock);
	}

	DrawKeycards(hud);

	if (ot == -1 || missionTime == 0) /* set the original time properly */
		ot = time(NULL);

	t = time(NULL);

	if (!isPaused)
	{
		td = t - ot;
	}

	sprintf(s, "%d:%02d", (int)(td / 60), (int)(td % 60));
	CDogsTextStringSpecial(s, TEXT_TOP | TEXT_XCENTER, 0, 5);
}
