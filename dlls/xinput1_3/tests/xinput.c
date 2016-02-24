/*
 * The Wine project - XInput Joystick Library
 *
 * Copyright 2008 Andrew Fenn
 * Copyright 2016 Juan Jose Gonzalez
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>
#include <stdio.h>

#include "xinput.h"
#include "wine/test.h"

#define ENUMSTRING(e) case (e): return #e;

static const int INTERACTIVE_LOOP_DELAY_MS = 100;
static const int INTERACTIVE_LOOP_COUNT = 150;
static const int INTERACTIVE_RUMBLE_DELAY_MS = 1000;

static DWORD (WINAPI *pXInputGetState)(DWORD, XINPUT_STATE*);
static DWORD (WINAPI *pXInputGetCapabilities)(DWORD,DWORD,XINPUT_CAPABILITIES*);
static DWORD (WINAPI *pXInputSetState)(DWORD, XINPUT_VIBRATION*);
static void  (WINAPI *pXInputEnable)(BOOL);
static DWORD (WINAPI *pXInputGetKeystroke)(DWORD, DWORD, PXINPUT_KEYSTROKE);
static DWORD (WINAPI *pXInputGetDSoundAudioDeviceGuids)(DWORD, GUID*, GUID*);
static DWORD (WINAPI *pXInputGetBatteryInformation)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);


static const char* get_vk_string(WORD VirtualKey) {
    switch (VirtualKey) {
        ENUMSTRING(VK_PAD_A)
        ENUMSTRING(VK_PAD_B)
        ENUMSTRING(VK_PAD_X)
        ENUMSTRING(VK_PAD_Y)
        ENUMSTRING(VK_PAD_RSHOULDER)
        ENUMSTRING(VK_PAD_LSHOULDER)
        ENUMSTRING(VK_PAD_LTRIGGER)
        ENUMSTRING(VK_PAD_RTRIGGER)
        ENUMSTRING(VK_PAD_DPAD_UP)
        ENUMSTRING(VK_PAD_DPAD_DOWN)
        ENUMSTRING(VK_PAD_DPAD_LEFT)
        ENUMSTRING(VK_PAD_DPAD_RIGHT)
        ENUMSTRING(VK_PAD_START)
        ENUMSTRING(VK_PAD_BACK)
        ENUMSTRING(VK_PAD_LTHUMB_PRESS)
        ENUMSTRING(VK_PAD_RTHUMB_PRESS)
        ENUMSTRING(VK_PAD_LTHUMB_UP)
        ENUMSTRING(VK_PAD_LTHUMB_DOWN)
        ENUMSTRING(VK_PAD_LTHUMB_RIGHT)
        ENUMSTRING(VK_PAD_LTHUMB_LEFT)
        ENUMSTRING(VK_PAD_LTHUMB_UPLEFT)
        ENUMSTRING(VK_PAD_LTHUMB_UPRIGHT)
        ENUMSTRING(VK_PAD_LTHUMB_DOWNRIGHT)
        ENUMSTRING(VK_PAD_LTHUMB_DOWNLEFT)
        ENUMSTRING(VK_PAD_RTHUMB_UP)
        ENUMSTRING(VK_PAD_RTHUMB_DOWN)
        ENUMSTRING(VK_PAD_RTHUMB_RIGHT)
        ENUMSTRING(VK_PAD_RTHUMB_LEFT)
        ENUMSTRING(VK_PAD_RTHUMB_UPLEFT)
        ENUMSTRING(VK_PAD_RTHUMB_UPRIGHT)
        ENUMSTRING(VK_PAD_RTHUMB_DOWNRIGHT)
        ENUMSTRING(VK_PAD_RTHUMB_DOWNLEFT)
    }
    return "--UNKNOWN--";
}

static void test_set_state(void)
{
    XINPUT_VIBRATION vibrator;
    DWORD controllerNum;
    DWORD result;

    pXInputEnable(1);

    for(controllerNum=0; controllerNum < XUSER_MAX_COUNT; controllerNum++)
    {
        ZeroMemory(&vibrator, sizeof(XINPUT_VIBRATION));

        if (winetest_interactive)
            trace("Controller %1d: Vibration sequence [left -> off -> left -> right -> off] in 1s intervals\n",
                controllerNum);

        vibrator.wLeftMotorSpeed = 0xffff;
        vibrator.wRightMotorSpeed = 0;
        result = pXInputSetState(controllerNum, &vibrator);
        ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED, "XInputSetState failed with (%d)\n", result);

        if (result == ERROR_DEVICE_NOT_CONNECTED) {
            skip("Controller %d is not connected\n", controllerNum);
            continue;
        }

        if (winetest_interactive)
            Sleep(INTERACTIVE_RUMBLE_DELAY_MS);

        pXInputEnable(0);

        if (winetest_interactive)
            Sleep(INTERACTIVE_RUMBLE_DELAY_MS);

        pXInputEnable(1);

        if (winetest_interactive)
            Sleep(INTERACTIVE_RUMBLE_DELAY_MS);

        vibrator.wLeftMotorSpeed = 0;
        vibrator.wRightMotorSpeed = 0xffff;
        result = pXInputSetState(controllerNum, &vibrator);
        ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED, "XInputSetState failed with (%d)\n", result);

        if (winetest_interactive)
            Sleep(INTERACTIVE_RUMBLE_DELAY_MS);

        vibrator.wLeftMotorSpeed = 0;
        vibrator.wRightMotorSpeed = 0;
        result = pXInputSetState(controllerNum, &vibrator);
        ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED, "XInputSetState failed with (%d)\n", result);
    }

    result = pXInputSetState(XUSER_MAX_COUNT+1, &vibrator);
    ok(result == ERROR_BAD_ARGUMENTS, "XInputSetState returned (%d)\n", result);
}

static void test_get_state(void)
{
    XINPUT_STATE controllerState;
    DWORD controllerNum;
    DWORD result;
    int i, count;
    char oldstate[248], curstate[248];

    for(controllerNum=0; controllerNum < XUSER_MAX_COUNT; controllerNum++)
    {
        if (winetest_interactive) {
            trace("Testing controller %1d\n", controllerNum);
            trace("You have %d seconds to test all axes, sliders, POVs and buttons\n",
                  (INTERACTIVE_LOOP_DELAY_MS * INTERACTIVE_LOOP_COUNT) / 1000);
            count = INTERACTIVE_LOOP_COUNT;
        } else
            count = 1;

        for (i = count; i > 0; i--) {
            ZeroMemory(&controllerState, sizeof(XINPUT_STATE));

            result = pXInputGetState(controllerNum, &controllerState);
            ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED, "XInputGetState failed with (%d)\n", result);

            if (ERROR_DEVICE_NOT_CONNECTED == result)
            {
                skip("Controller %d is not connected\n", controllerNum);
                break;
            }
            else
            {
                sprintf(curstate, "PN0x%08x: BTN0x%04x"
                    "  LX%6d  LY%6d  RX%6d  RY%6d  LT%3u  RT%3u",
                    controllerState.dwPacketNumber,
                    controllerState.Gamepad.wButtons,
                    controllerState.Gamepad.sThumbLX,
                    controllerState.Gamepad.sThumbLY,
                    controllerState.Gamepad.sThumbRX,
                    controllerState.Gamepad.sThumbRY,
                    controllerState.Gamepad.bLeftTrigger,
                    controllerState.Gamepad.bRightTrigger);
                if (strcmp(oldstate, curstate) != 0) {
                    trace("%s\n", curstate);
                    strcpy(oldstate, curstate);
                }
            }

            if (winetest_interactive)
                Sleep(INTERACTIVE_LOOP_DELAY_MS);
        }
    }

    ZeroMemory(&controllerState, sizeof(XINPUT_STATE));
    result = pXInputGetState(XUSER_MAX_COUNT+1, &controllerState);
    ok(result == ERROR_BAD_ARGUMENTS, "XInputGetState returned (%d)\n", result);
}

static void test_get_keystroke_single(DWORD controllerNum) {
    XINPUT_KEYSTROKE keystroke;
    DWORD result;
    int i, count;
    const char *flagsString;

    if (winetest_interactive) {
        if (controllerNum == XUSER_INDEX_ANY)
            trace("Testing all controllers at the same time\n");
        else
            trace("Testing controller %1d\n", controllerNum);
        trace("You have %d seconds to test all axes, sliders, POVs and buttons\n",
                  (INTERACTIVE_LOOP_DELAY_MS * INTERACTIVE_LOOP_COUNT) / 1000);
        count = INTERACTIVE_LOOP_COUNT;
    } else
        count = 1;

    for (i = count; i > 0; i--) {
        ZeroMemory(&keystroke, sizeof(XINPUT_KEYSTROKE));

        /* Checking for keystroke.Flags != 0 due to a bug in Microsoft's xinput1_3 XInputGetKeystroke
         * function that returns ERROR_SUCCESS and Flags = 0 instead of ERROR_DEVICE_NOT_CONNECTED */
        while ((result = pXInputGetKeystroke(controllerNum, XINPUT_FLAG_GAMEPAD, &keystroke))
            == ERROR_SUCCESS && keystroke.Flags != 0) {
            switch (keystroke.Flags) {
                case XINPUT_KEYSTROKE_KEYDOWN:
                    flagsString = "D ";
                    break;
                case XINPUT_KEYSTROKE_KEYDOWN | XINPUT_KEYSTROKE_REPEAT:
                    flagsString = "DR";
                    break;
                case XINPUT_KEYSTROKE_KEYUP:
                    flagsString = "U ";
                    break;
                case XINPUT_KEYSTROKE_KEYUP | XINPUT_KEYSTROKE_REPEAT:
                    flagsString = "UR";
                    break;
                default:
                    flagsString = NULL;
            }
            ok(flagsString != NULL, "XInputGetKeystroke set Flags to (%d)\n",
               keystroke.Flags);
            if (flagsString == NULL)
                break;

            trace("%s: VK(%5d: %s) Unicode(%4x) Idx(%1d) HidCode(%2x)\n",
                flagsString, keystroke.VirtualKey, get_vk_string(keystroke.VirtualKey), keystroke.Unicode,
                keystroke.UserIndex, keystroke.HidCode);
        }
        ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED || result == ERROR_EMPTY,
           "XInputGetKeystroke failed with (%d)\n", result);
        if (result != ERROR_SUCCESS && result != ERROR_DEVICE_NOT_CONNECTED && result != ERROR_EMPTY)
            break;

        if (result == ERROR_DEVICE_NOT_CONNECTED || (result == ERROR_SUCCESS && keystroke.Flags == 0)) {
            skip("Controller %d is not connected\n", controllerNum);
            break;
        }

        if (winetest_interactive)
            Sleep(INTERACTIVE_LOOP_DELAY_MS);
    }
}

static void test_get_keystroke(void)
{
    XINPUT_KEYSTROKE keystroke;
    DWORD controllerNum;
    DWORD result;

    for(controllerNum=0; controllerNum < XUSER_MAX_COUNT; controllerNum++)
    {
        test_get_keystroke_single(controllerNum);
    }
    test_get_keystroke_single(XUSER_INDEX_ANY);

    ZeroMemory(&keystroke, sizeof(XINPUT_KEYSTROKE));
    result = pXInputGetKeystroke(XUSER_MAX_COUNT+1, XINPUT_FLAG_GAMEPAD, &keystroke);
    ok(result == ERROR_BAD_ARGUMENTS, "XInputGetKeystroke returned (%d)\n", result);
}

static void test_get_capabilities(void)
{
    XINPUT_CAPABILITIES capabilities;
    DWORD controllerNum;
    DWORD result;

    for(controllerNum=0; controllerNum < XUSER_MAX_COUNT; controllerNum++)
    {
        ZeroMemory(&capabilities, sizeof(XINPUT_CAPABILITIES));

        result = pXInputGetCapabilities(controllerNum, XINPUT_FLAG_GAMEPAD, &capabilities);
        ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED, "XInputGetCapabilities failed with (%d)\n", result);

        if (ERROR_DEVICE_NOT_CONNECTED == result)
        {
            skip("Controller %d is not connected\n", controllerNum);
        } else {
            ok(capabilities.Type == XINPUT_DEVTYPE_GAMEPAD, "XInputGetCapabilities returned Type (%d)\n",
               capabilities.Type);
        }
    }

    ZeroMemory(&capabilities, sizeof(XINPUT_CAPABILITIES));
    result = pXInputGetCapabilities(XUSER_MAX_COUNT+1, XINPUT_FLAG_GAMEPAD, &capabilities);
    ok(result == ERROR_BAD_ARGUMENTS, "XInputGetCapabilities returned (%d)\n", result);
}

static void test_get_dsoundaudiodevice(void)
{
    DWORD controllerNum;
    DWORD result;
    GUID soundRender;
    GUID soundCapture;

    for(controllerNum=0; controllerNum < XUSER_MAX_COUNT; controllerNum++)
    {
        result = pXInputGetDSoundAudioDeviceGuids(controllerNum, &soundRender, &soundCapture);
        ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED, "XInputGetDSoundAudioDeviceGuids failed with (%d)\n", result);

        if (ERROR_DEVICE_NOT_CONNECTED == result)
        {
            skip("Controller %d is not connected\n", controllerNum);
        }
    }

    result = pXInputGetDSoundAudioDeviceGuids(XUSER_MAX_COUNT+1, &soundRender, &soundCapture);
    ok(result == ERROR_BAD_ARGUMENTS, "XInputGetDSoundAudioDeviceGuids returned (%d)\n", result);
}

static void test_get_batteryinformation(void)
{
    DWORD controllerNum;
    DWORD result;
    XINPUT_BATTERY_INFORMATION batteryInfo;

    for(controllerNum=0; controllerNum < XUSER_MAX_COUNT; controllerNum++)
    {
        ZeroMemory(&batteryInfo, sizeof(XINPUT_BATTERY_INFORMATION));

        result = pXInputGetBatteryInformation(controllerNum, BATTERY_DEVTYPE_GAMEPAD, &batteryInfo);
        ok(result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED, "XInputGetBatteryInformation failed with (%d)\n", result);

        if (ERROR_DEVICE_NOT_CONNECTED == result) {
            ok(batteryInfo.BatteryType == BATTERY_TYPE_DISCONNECTED, "Failed to report device as being disconnected.\n");
            skip("Controller %d is not connected\n", controllerNum);
        } else if (ERROR_SUCCESS == result)
            trace("Controller %d Battery: Type=%3u  Level=%3u\n", controllerNum, batteryInfo.BatteryType,
                  batteryInfo.BatteryLevel);
    }

    result = pXInputGetBatteryInformation(XUSER_MAX_COUNT+1, BATTERY_DEVTYPE_GAMEPAD, &batteryInfo);
    ok(result == ERROR_BAD_ARGUMENTS, "XInputGetBatteryInformation returned (%d)\n", result);
}

START_TEST(xinput)
{
    HMODULE hXinput;
    hXinput = LoadLibraryA( "xinput1_3.dll" );

    if (!hXinput)
    {
        win_skip("Could not load xinput1_3.dll\n");
        return;
    }

    pXInputEnable = (void*)GetProcAddress(hXinput, "XInputEnable");
    pXInputSetState = (void*)GetProcAddress(hXinput, "XInputSetState");
    pXInputGetState = (void*)GetProcAddress(hXinput, "XInputGetState");
    pXInputGetKeystroke = (void*)GetProcAddress(hXinput, "XInputGetKeystroke");
    pXInputGetCapabilities = (void*)GetProcAddress(hXinput, "XInputGetCapabilities");
    pXInputGetDSoundAudioDeviceGuids = (void*)GetProcAddress(hXinput, "XInputGetDSoundAudioDeviceGuids");
    pXInputGetBatteryInformation = (void*)GetProcAddress(hXinput, "XInputGetBatteryInformation");

    test_set_state();
    test_get_state();
    test_get_keystroke();
    test_get_capabilities();
    test_get_dsoundaudiodevice();
    test_get_batteryinformation();

    FreeLibrary(hXinput);
}
