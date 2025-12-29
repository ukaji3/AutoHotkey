/*
AutoHotkey

Touchpad contact detection via Raw Input API for Windows Precision Touchpad.
Uses dynamic loading of hid.dll to avoid header conflicts.
*/

#include "stdafx.h"
#include "touchpad.h"
#include "globaldata.h"

// HID API types and constants (avoid including hidpi.h due to conflicts)
typedef LONG NTSTATUS;
#define HIDP_STATUS_SUCCESS ((NTSTATUS)0x00110000)

typedef struct _HIDP_PREPARSED_DATA *PHIDP_PREPARSED_DATA;

typedef struct _HIDP_CAPS {
    USHORT Usage;
    USHORT UsagePage;
    USHORT InputReportByteLength;
    USHORT OutputReportByteLength;
    USHORT FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT NumberInputButtonCaps;
    USHORT NumberInputValueCaps;
    USHORT NumberInputDataIndices;
    USHORT NumberOutputButtonCaps;
    USHORT NumberOutputValueCaps;
    USHORT NumberOutputDataIndices;
    USHORT NumberFeatureButtonCaps;
    USHORT NumberFeatureValueCaps;
    USHORT NumberFeatureDataIndices;
} HIDP_CAPS, *PHIDP_CAPS;

typedef enum _HIDP_REPORT_TYPE {
    HidP_Input,
    HidP_Output,
    HidP_Feature
} HIDP_REPORT_TYPE;

typedef struct _HIDP_VALUE_CAPS {
    USHORT UsagePage;
    UCHAR ReportID;
    BOOLEAN IsAlias;
    USHORT BitField;
    USHORT LinkCollection;
    USHORT LinkUsage;
    USHORT LinkUsagePage;
    BOOLEAN IsRange;
    BOOLEAN IsStringRange;
    BOOLEAN IsDesignatorRange;
    BOOLEAN IsAbsolute;
    BOOLEAN HasNull;
    UCHAR Reserved;
    USHORT BitSize;
    USHORT ReportCount;
    USHORT Reserved2[5];
    ULONG UnitsExp;
    ULONG Units;
    LONG LogicalMin;
    LONG LogicalMax;
    LONG PhysicalMin;
    LONG PhysicalMax;
    union {
        struct {
            USHORT UsageMin;
            USHORT UsageMax;
            USHORT StringMin;
            USHORT StringMax;
            USHORT DesignatorMin;
            USHORT DesignatorMax;
            USHORT DataIndexMin;
            USHORT DataIndexMax;
        } Range;
        struct {
            USHORT Usage;
            USHORT Reserved1;
            USHORT StringIndex;
            USHORT Reserved2;
            USHORT DesignatorIndex;
            USHORT Reserved3;
            USHORT DataIndex;
            USHORT Reserved4;
        } NotRange;
    };
} HIDP_VALUE_CAPS, *PHIDP_VALUE_CAPS;

// Function pointer types for hid.dll
typedef NTSTATUS (WINAPI *PFN_HidP_GetCaps)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);
typedef NTSTATUS (WINAPI *PFN_HidP_GetValueCaps)(HIDP_REPORT_TYPE, PHIDP_VALUE_CAPS, PUSHORT, PHIDP_PREPARSED_DATA);
typedef NTSTATUS (WINAPI *PFN_HidP_GetUsageValue)(HIDP_REPORT_TYPE, USHORT, USHORT, USHORT, PULONG, PHIDP_PREPARSED_DATA, PCHAR, ULONG);

// Global function pointers
static HMODULE sHidDll = NULL;
static PFN_HidP_GetCaps pHidP_GetCaps = NULL;
static PFN_HidP_GetValueCaps pHidP_GetValueCaps = NULL;
static PFN_HidP_GetUsageValue pHidP_GetUsageValue = NULL;

// Global touchpad state
TouchpadState g_Touchpad = {0, false, 0, false, NULL};

// Preparsed data for HID report parsing
static PHIDP_PREPARSED_DATA sPreparsedData = NULL;
static HIDP_CAPS sHidCaps = {0};
static bool sHidCapsValid = false;

// Value caps for contact count
static HIDP_VALUE_CAPS *sValueCaps = NULL;
static USHORT sValueCapsLength = 0;
static USHORT sContactCountValueIndex = (USHORT)-1;

// Timeout for contact detection (ms)
// If no report received within this time, assume no contact
#define TOUCHPAD_CONTACT_TIMEOUT 100

// Timer ID for contact timeout
#define TOUCHPAD_TIMER_ID 0x7F01
static HWND sTimerHwnd = NULL;


static bool LoadHidDll()
{
    if (sHidDll)
        return true;
    
    sHidDll = LoadLibrary(_T("hid.dll"));
    if (!sHidDll)
        return false;
    
    pHidP_GetCaps = (PFN_HidP_GetCaps)GetProcAddress(sHidDll, "HidP_GetCaps");
    pHidP_GetValueCaps = (PFN_HidP_GetValueCaps)GetProcAddress(sHidDll, "HidP_GetValueCaps");
    pHidP_GetUsageValue = (PFN_HidP_GetUsageValue)GetProcAddress(sHidDll, "HidP_GetUsageValue");
    
    if (!pHidP_GetCaps || !pHidP_GetValueCaps || !pHidP_GetUsageValue)
    {
        FreeLibrary(sHidDll);
        sHidDll = NULL;
        return false;
    }
    
    return true;
}


static void CleanupHidResources()
{
    if (sValueCaps)
    {
        free(sValueCaps);
        sValueCaps = NULL;
    }
    sValueCapsLength = 0;
    sContactCountValueIndex = (USHORT)-1;
    sHidCapsValid = false;
}


// Timer callback for contact timeout
static VOID CALLBACK TouchpadTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    // Check if we haven't received a report recently
    DWORD elapsed = GetTickCount() - g_Touchpad.LastContactTime;
    if (elapsed >= TOUCHPAD_CONTACT_TIMEOUT && g_Touchpad.IsContactActive)
    {
        // No recent report - assume contact ended
        g_Touchpad.ContactCount = 0;
        g_Touchpad.IsContactActive = false;
    }
}


bool TouchpadInit(HWND hWnd)
{
    // Load hid.dll dynamically
    if (!LoadHidDll())
    {
        g_Touchpad.Initialized = false;
        return false;
    }
    
    // Register for Raw Input from Precision Touchpad devices
    RAWINPUTDEVICE rid = {0};
    rid.usUsagePage = HID_USAGE_PAGE_DIGITIZER;
    rid.usUsage = HID_USAGE_DIGITIZER_TOUCHPAD;
    rid.dwFlags = RIDEV_INPUTSINK; // Receive input even when not in foreground
    rid.hwndTarget = hWnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
    {
        g_Touchpad.Initialized = false;
        return false;
    }

    g_Touchpad.Initialized = true;
    g_Touchpad.ContactCount = 0;
    g_Touchpad.IsContactActive = false;
    g_Touchpad.LastContactTime = GetTickCount();
    
    // Start timer for contact timeout detection
    sTimerHwnd = hWnd;
    SetTimer(hWnd, TOUCHPAD_TIMER_ID, TOUCHPAD_CONTACT_TIMEOUT / 2, TouchpadTimerProc);
    
    return true;
}


void TouchpadCleanup()
{
    if (g_Touchpad.Initialized)
    {
        // Kill the timeout timer
        if (sTimerHwnd)
        {
            KillTimer(sTimerHwnd, TOUCHPAD_TIMER_ID);
            sTimerHwnd = NULL;
        }
        
        // Unregister Raw Input device
        RAWINPUTDEVICE rid = {0};
        rid.usUsagePage = HID_USAGE_PAGE_DIGITIZER;
        rid.usUsage = HID_USAGE_DIGITIZER_TOUCHPAD;
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = NULL;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        
        g_Touchpad.Initialized = false;
    }
    
    CleanupHidResources();
    
    if (sPreparsedData)
    {
        free(sPreparsedData);
        sPreparsedData = NULL;
    }
    
    if (sHidDll)
    {
        FreeLibrary(sHidDll);
        sHidDll = NULL;
        pHidP_GetCaps = NULL;
        pHidP_GetValueCaps = NULL;
        pHidP_GetUsageValue = NULL;
    }
}


static bool InitPreparsedData(HANDLE hDevice)
{
    if (!pHidP_GetCaps || !pHidP_GetValueCaps)
        return false;
    
    // Get preparsed data size
    UINT dataSize = 0;
    if (GetRawInputDeviceInfo(hDevice, RIDI_PREPARSEDDATA, NULL, &dataSize) != 0)
        return false;
    
    if (dataSize == 0)
        return false;
    
    // Allocate and get preparsed data
    PHIDP_PREPARSED_DATA newPreparsedData = (PHIDP_PREPARSED_DATA)malloc(dataSize);
    if (!newPreparsedData)
        return false;
    
    if (GetRawInputDeviceInfo(hDevice, RIDI_PREPARSEDDATA, newPreparsedData, &dataSize) == (UINT)-1)
    {
        free(newPreparsedData);
        return false;
    }
    
    // Get HID capabilities
    HIDP_CAPS caps;
    if (pHidP_GetCaps(newPreparsedData, &caps) != HIDP_STATUS_SUCCESS)
    {
        free(newPreparsedData);
        return false;
    }
    
    // Clean up old data
    CleanupHidResources();
    if (sPreparsedData)
        free(sPreparsedData);
    
    sPreparsedData = newPreparsedData;
    sHidCaps = caps;
    sHidCapsValid = true;
    g_Touchpad.DeviceHandle = hDevice;
    
    // Get value caps for contact count
    if (caps.NumberInputValueCaps > 0)
    {
        sValueCapsLength = caps.NumberInputValueCaps;
        sValueCaps = (HIDP_VALUE_CAPS*)malloc(sizeof(HIDP_VALUE_CAPS) * sValueCapsLength);
        if (sValueCaps)
        {
            USHORT capsLength = sValueCapsLength;
            if (pHidP_GetValueCaps(HidP_Input, sValueCaps, &capsLength, sPreparsedData) == HIDP_STATUS_SUCCESS)
            {
                sValueCapsLength = capsLength;
                // Find contact count usage
                for (USHORT i = 0; i < sValueCapsLength; i++)
                {
                    if (sValueCaps[i].UsagePage == HID_USAGE_PAGE_DIGITIZER &&
                        sValueCaps[i].NotRange.Usage == HID_USAGE_DIGITIZER_CONTACT_COUNT)
                    {
                        sContactCountValueIndex = i;
                        break;
                    }
                }
            }
        }
    }
    
    return true;
}


bool TouchpadProcessRawInput(LPARAM lParam)
{
    if (!g_Touchpad.Initialized || !pHidP_GetUsageValue)
        return false;
    
    // Get raw input data size
    UINT dataSize = 0;
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dataSize, sizeof(RAWINPUTHEADER)) != 0)
        return false;
    
    if (dataSize == 0)
        return false;
    
    // Allocate buffer for raw input data
    BYTE *rawData = (BYTE*)_alloca(dataSize);
    if (!rawData)
        return false;
    
    // Get raw input data
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawData, &dataSize, sizeof(RAWINPUTHEADER)) != dataSize)
        return false;
    
    RAWINPUT *raw = (RAWINPUT*)rawData;
    
    // Only process HID input (touchpad)
    if (raw->header.dwType != RIM_TYPEHID)
        return false;
    
    // Verify this is a digitizer device by checking device info
    RID_DEVICE_INFO deviceInfo = {0};
    deviceInfo.cbSize = sizeof(RID_DEVICE_INFO);
    UINT infoSize = sizeof(RID_DEVICE_INFO);
    if (GetRawInputDeviceInfo(raw->header.hDevice, RIDI_DEVICEINFO, &deviceInfo, &infoSize) == (UINT)-1)
        return false;
    
    // Check if this is a touchpad (digitizer usage page)
    if (deviceInfo.dwType != RIM_TYPEHID ||
        deviceInfo.hid.usUsagePage != HID_USAGE_PAGE_DIGITIZER ||
        deviceInfo.hid.usUsage != HID_USAGE_DIGITIZER_TOUCHPAD)
        return false;
    
    // Initialize preparsed data if needed or if device changed
    if (!sHidCapsValid || g_Touchpad.DeviceHandle != raw->header.hDevice)
    {
        if (!InitPreparsedData(raw->header.hDevice))
            return false;
    }
    
    // Parse HID report
    BYTE *reportData = raw->data.hid.bRawData;
    DWORD reportSize = raw->data.hid.dwSizeHid;
    DWORD reportCount = raw->data.hid.dwCount;
    
    int newContactCount = 0;
    bool foundContactCount = false;
    
    // Process each report
    for (DWORD i = 0; i < reportCount; i++)
    {
        BYTE *currentReport = reportData + (i * reportSize);
        
        // Try to get contact count
        if (sContactCountValueIndex != (USHORT)-1)
        {
            ULONG value = 0;
            NTSTATUS status = pHidP_GetUsageValue(
                HidP_Input,
                HID_USAGE_PAGE_DIGITIZER,
                0, // Link collection
                HID_USAGE_DIGITIZER_CONTACT_COUNT,
                &value,
                sPreparsedData,
                (PCHAR)currentReport,
                reportSize
            );
            
            if (status == HIDP_STATUS_SUCCESS)
            {
                newContactCount = (int)value;
                foundContactCount = true;
            }
        }
    }
    
    // Update timestamp - we received a report from the touchpad
    g_Touchpad.LastContactTime = GetTickCount();
    
    // Update global state if we got valid data
    if (foundContactCount)
    {
        g_Touchpad.ContactCount = newContactCount;
        g_Touchpad.IsContactActive = (newContactCount > 0);
        return true;
    }
    
    // Even if contact count wasn't found, receiving a report likely means contact is active
    // (touchpads typically only send reports when touched)
    if (!g_Touchpad.IsContactActive)
    {
        g_Touchpad.ContactCount = 1; // Assume at least 1 contact
        g_Touchpad.IsContactActive = true;
    }
    
    return true;
}
