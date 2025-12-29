/*
AutoHotkey

Touchpad contact detection via Raw Input API for Windows Precision Touchpad.

This module provides the ability to detect when fingers are touching the touchpad
surface, enabling "touchpad as modifier" functionality.
*/

#ifndef touchpad_h
#define touchpad_h

#include "stdafx.h"

// HID Usage Pages and Usages for Precision Touchpad
// Use #ifndef to avoid conflicts with Windows SDK headers
#ifndef HID_USAGE_PAGE_DIGITIZER
#define HID_USAGE_PAGE_DIGITIZER    0x0D
#endif
#ifndef HID_USAGE_DIGITIZER_TOUCHPAD
#define HID_USAGE_DIGITIZER_TOUCHPAD 0x05
#endif
#ifndef HID_USAGE_DIGITIZER_TIP
#define HID_USAGE_DIGITIZER_TIP     0x42
#endif
#ifndef HID_USAGE_DIGITIZER_CONTACT_COUNT
#define HID_USAGE_DIGITIZER_CONTACT_COUNT 0x54
#endif
#ifndef HID_USAGE_DIGITIZER_CONTACT_ID
#define HID_USAGE_DIGITIZER_CONTACT_ID 0x51
#endif

// Maximum number of simultaneous contacts to track
#define TOUCHPAD_MAX_CONTACTS 5

// Touchpad state structure
struct TouchpadState
{
    int ContactCount;           // Number of fingers currently touching
    bool IsContactActive;       // True if any finger is touching
    DWORD LastContactTime;      // Tick count of last contact change
    bool Initialized;           // True if Raw Input registration succeeded
    HANDLE DeviceHandle;        // Handle to the touchpad device (if found)
};

// Global touchpad state
extern TouchpadState g_Touchpad;

// Initialize touchpad detection (call during WM_CREATE)
bool TouchpadInit(HWND hWnd);

// Cleanup touchpad resources
void TouchpadCleanup();

// Process WM_INPUT message for touchpad data
// Returns true if the message was a touchpad message and was processed
bool TouchpadProcessRawInput(LPARAM lParam);

// Get current contact count
inline int TouchpadGetContactCount() { return g_Touchpad.ContactCount; }

// Check if touchpad has any contact
inline bool TouchpadHasContact() { return g_Touchpad.IsContactActive; }

// Check if touchpad detection is available
inline bool TouchpadIsAvailable() { return g_Touchpad.Initialized; }

#endif // touchpad_h
