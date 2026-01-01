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

// HIDP_BUTTON_CAPS structure for button capability enumeration (Task 2.1)
// Compatible with hidpi.h definition
typedef struct _HIDP_BUTTON_CAPS {
    USHORT UsagePage;
    UCHAR  ReportID;
    BOOLEAN IsAlias;
    USHORT BitField;
    USHORT LinkCollection;
    USHORT LinkUsage;
    USHORT LinkUsagePage;
    BOOLEAN IsRange;
    BOOLEAN IsStringRange;
    BOOLEAN IsDesignatorRange;
    BOOLEAN IsAbsolute;
    ULONG  Reserved[10];
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
} HIDP_BUTTON_CAPS, *PHIDP_BUTTON_CAPS;

// Function pointer types for hid.dll
typedef NTSTATUS (WINAPI *PFN_HidP_GetCaps)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);
typedef NTSTATUS (WINAPI *PFN_HidP_GetValueCaps)(HIDP_REPORT_TYPE, PHIDP_VALUE_CAPS, PUSHORT, PHIDP_PREPARSED_DATA);
typedef NTSTATUS (WINAPI *PFN_HidP_GetUsageValue)(HIDP_REPORT_TYPE, USHORT, USHORT, USHORT, PULONG, PHIDP_PREPARSED_DATA, PCHAR, ULONG);

// Function pointer types for button API (Task 2.2)
typedef NTSTATUS (WINAPI *PFN_HidP_GetButtonCaps)(HIDP_REPORT_TYPE, PHIDP_BUTTON_CAPS, PUSHORT, PHIDP_PREPARSED_DATA);
typedef NTSTATUS (WINAPI *PFN_HidP_GetUsages)(HIDP_REPORT_TYPE, USHORT, USHORT, PUSHORT, PULONG, PHIDP_PREPARSED_DATA, PCHAR, ULONG);

// Global function pointers
static HMODULE sHidDll = NULL;
static PFN_HidP_GetCaps pHidP_GetCaps = NULL;
static PFN_HidP_GetValueCaps pHidP_GetValueCaps = NULL;
static PFN_HidP_GetUsageValue pHidP_GetUsageValue = NULL;

// Global function pointers for button API (Task 2.2)
static PFN_HidP_GetButtonCaps pHidP_GetButtonCaps = NULL;
static PFN_HidP_GetUsages pHidP_GetUsages = NULL;

// Link collection mapping for Confidence bit (Task 3.1)
// Tracks which Link Collections contain Confidence and TipSwitch usages
struct LinkCollectionInfo {
    USHORT LinkCollection;      // Link Collection number
    bool HasConfidence;         // True if this collection has Confidence usage (0x47)
    bool HasTipSwitch;          // True if this collection has Tip Switch usage (0x42)
    bool IsValid;               // True if this entry is in use
};

// Contact tracking for palm rejection (Task 3.1)
// Tracks the state of each contact by Contact ID
struct ContactInfo {
    USHORT ContactId;           // Contact ID from HID report
    bool IsRejected;            // True if Confidence=0 was ever detected for this contact
    bool IsActive;              // True if currently touching (Tip Switch=1)
};

// Static arrays for Link Collection and Contact tracking (Task 3.1)
// Maximum 5 Link Collections (one per finger) and 5 Contacts
static LinkCollectionInfo sLinkCollections[TOUCHPAD_MAX_CONTACTS] = {0};
static USHORT sLinkCollectionCount = 0;

static ContactInfo sContacts[TOUCHPAD_MAX_CONTACTS] = {0};
static USHORT sContactInfoCount = 0;

// Global touchpad state
TouchpadState g_Touchpad = {0, 0, false, 0, false, NULL, false};

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
    
    // Load button API functions (Task 2.3)
    pHidP_GetButtonCaps = (PFN_HidP_GetButtonCaps)GetProcAddress(sHidDll, "HidP_GetButtonCaps");
    pHidP_GetUsages = (PFN_HidP_GetUsages)GetProcAddress(sHidDll, "HidP_GetUsages");
    
    if (!pHidP_GetCaps || !pHidP_GetValueCaps || !pHidP_GetUsageValue)
    {
        FreeLibrary(sHidDll);
        sHidDll = NULL;
        return false;
    }
    
    // Note: pHidP_GetButtonCaps and pHidP_GetUsages are optional for backward compatibility
    // If they fail to load, palm rejection will fall back to ContactCount mode
    
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
    
    // Clear Link Collection tracking (Task 3.1)
    memset(sLinkCollections, 0, sizeof(sLinkCollections));
    sLinkCollectionCount = 0;
    
    // Clear Contact tracking (Task 3.1)
    memset(sContacts, 0, sizeof(sContacts));
    sContactInfoCount = 0;
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
    g_Touchpad.FingerCount = 0;
    g_Touchpad.IsContactActive = false;
    g_Touchpad.LastContactTime = GetTickCount();
    g_Touchpad.ConfidenceSupported = false;
    
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
        pHidP_GetButtonCaps = NULL;
        pHidP_GetUsages = NULL;
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
    
    // Get button caps to find Confidence and TipSwitch usages (Task 3.2)
    // This enables palm rejection by identifying which Link Collections have Confidence bit
    g_Touchpad.ConfidenceSupported = false;
    sLinkCollectionCount = 0;
    
    if (pHidP_GetButtonCaps && caps.NumberInputButtonCaps > 0)
    {
        USHORT buttonCapsLength = caps.NumberInputButtonCaps;
        HIDP_BUTTON_CAPS *buttonCaps = (HIDP_BUTTON_CAPS*)malloc(sizeof(HIDP_BUTTON_CAPS) * buttonCapsLength);
        if (buttonCaps)
        {
            USHORT capsLength = buttonCapsLength;
            if (pHidP_GetButtonCaps(HidP_Input, buttonCaps, &capsLength, sPreparsedData) == HIDP_STATUS_SUCCESS)
            {
                // Scan button caps to find Confidence (0x47) and TipSwitch (0x42) usages
                // and record which Link Collections contain them
                for (USHORT i = 0; i < capsLength; i++)
                {
                    HIDP_BUTTON_CAPS *cap = &buttonCaps[i];
                    
                    // Only process Digitizer usage page
                    if (cap->UsagePage != HID_USAGE_PAGE_DIGITIZER)
                        continue;
                    
                    // Get the usage (handle both Range and NotRange cases)
                    USHORT usage = cap->IsRange ? cap->Range.UsageMin : cap->NotRange.Usage;
                    USHORT usageMax = cap->IsRange ? cap->Range.UsageMax : cap->NotRange.Usage;
                    
                    // Check if this is Confidence or TipSwitch
                    bool isConfidence = (usage <= HID_USAGE_DIGITIZER_CONFIDENCE && usageMax >= HID_USAGE_DIGITIZER_CONFIDENCE);
                    bool isTipSwitch = (usage <= HID_USAGE_DIGITIZER_TIP && usageMax >= HID_USAGE_DIGITIZER_TIP);
                    
                    if (!isConfidence && !isTipSwitch)
                        continue;
                    
                    // Find or create LinkCollectionInfo entry for this Link Collection
                    LinkCollectionInfo *linkInfo = NULL;
                    for (USHORT j = 0; j < sLinkCollectionCount; j++)
                    {
                        if (sLinkCollections[j].LinkCollection == cap->LinkCollection)
                        {
                            linkInfo = &sLinkCollections[j];
                            break;
                        }
                    }
                    
                    // Create new entry if not found and we have space
                    if (!linkInfo && sLinkCollectionCount < TOUCHPAD_MAX_CONTACTS)
                    {
                        linkInfo = &sLinkCollections[sLinkCollectionCount];
                        linkInfo->LinkCollection = cap->LinkCollection;
                        linkInfo->HasConfidence = false;
                        linkInfo->HasTipSwitch = false;
                        linkInfo->IsValid = true;
                        sLinkCollectionCount++;
                    }
                    
                    // Update the entry
                    if (linkInfo)
                    {
                        if (isConfidence)
                        {
                            linkInfo->HasConfidence = true;
                            g_Touchpad.ConfidenceSupported = true;
                        }
                        if (isTipSwitch)
                        {
                            linkInfo->HasTipSwitch = true;
                        }
                    }
                }
            }
            free(buttonCaps);
        }
    }
    
    // If Confidence bit is not supported, fall back to ContactCount mode (Requirement 5.1)
    // FingerCount will equal ContactCount in this case
    if (!g_Touchpad.ConfidenceSupported)
    {
        // Debug log output for fallback mode (Requirement 5.3)
        TRACE(_T("Touchpad: Confidence bit not supported, falling back to ContactCount mode\n"));
    }
    
    return true;
}


// Helper function to find or create a ContactInfo entry for a given Contact ID
// Returns pointer to the ContactInfo, or NULL if no space available
static ContactInfo* FindOrCreateContact(USHORT contactId)
{
    // First, look for existing entry with this Contact ID
    for (USHORT i = 0; i < sContactInfoCount; i++)
    {
        if (sContacts[i].ContactId == contactId)
            return &sContacts[i];
    }
    
    // Not found - create new entry if we have space
    if (sContactInfoCount < TOUCHPAD_MAX_CONTACTS)
    {
        ContactInfo *contact = &sContacts[sContactInfoCount];
        contact->ContactId = contactId;
        contact->IsRejected = false;
        contact->IsActive = false;
        sContactInfoCount++;
        return contact;
    }
    
    // No space - try to find an inactive entry to reuse (LRU-style)
    for (USHORT i = 0; i < TOUCHPAD_MAX_CONTACTS; i++)
    {
        if (!sContacts[i].IsActive)
        {
            sContacts[i].ContactId = contactId;
            sContacts[i].IsRejected = false;
            sContacts[i].IsActive = false;
            return &sContacts[i];
        }
    }
    
    return NULL; // All slots are active, cannot track this contact
}


// Helper function to clear a contact when it's released (Tip Switch = 0)
// This removes the contact from the rejected list, allowing fresh evaluation
static void ClearContact(USHORT contactId)
{
    for (USHORT i = 0; i < sContactInfoCount; i++)
    {
        if (sContacts[i].ContactId == contactId)
        {
            // Mark as inactive and clear rejection state
            sContacts[i].IsActive = false;
            sContacts[i].IsRejected = false;
            return;
        }
    }
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
    int newFingerCount = 0;
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
        
        // Task 4.1: Process Confidence and TipSwitch for each Link Collection
        // Only if we have the HidP_GetUsages function and Link Collections with Confidence
        if (pHidP_GetUsages && sLinkCollectionCount > 0)
        {
            // Reset finger count for this report - count valid fingers across all Link Collections
            int reportFingerCount = 0;
            
            // Process each Link Collection that has Confidence/TipSwitch
            for (USHORT lcIdx = 0; lcIdx < sLinkCollectionCount; lcIdx++)
            {
                LinkCollectionInfo *linkInfo = &sLinkCollections[lcIdx];
                if (!linkInfo->IsValid)
                    continue;
                
                // Get active button usages for this Link Collection
                // Buffer for returned usages (Confidence=0x47, TipSwitch=0x42, etc.)
                USHORT usageList[16] = {0};
                ULONG usageLength = 16;
                
                NTSTATUS status = pHidP_GetUsages(
                    HidP_Input,
                    HID_USAGE_PAGE_DIGITIZER,
                    linkInfo->LinkCollection,
                    usageList,
                    &usageLength,
                    sPreparsedData,
                    (PCHAR)currentReport,
                    reportSize
                );
                
                // Check if TipSwitch (0x42) and Confidence (0x47) are in the returned list
                bool hasTipSwitch = false;
                bool hasConfidence = false;
                
                if (status == HIDP_STATUS_SUCCESS)
                {
                    for (ULONG u = 0; u < usageLength; u++)
                    {
                        if (usageList[u] == HID_USAGE_DIGITIZER_TIP)
                            hasTipSwitch = true;
                        if (usageList[u] == HID_USAGE_DIGITIZER_CONFIDENCE)
                            hasConfidence = true;
                    }
                }
                else
                {
                    // HidP_GetUsages failed - skip this Link Collection
                    // Don't assume anything about this contact
                    continue;
                }
                
                // Only process if TipSwitch is active (finger is touching)
                if (!hasTipSwitch)
                    continue;
                
                // Get Contact ID for this Link Collection
                ULONG contactIdValue = 0;
                status = pHidP_GetUsageValue(
                    HidP_Input,
                    HID_USAGE_PAGE_DIGITIZER,
                    linkInfo->LinkCollection,
                    HID_USAGE_DIGITIZER_CONTACT_ID,
                    &contactIdValue,
                    sPreparsedData,
                    (PCHAR)currentReport,
                    reportSize
                );
                
                if (status != HIDP_STATUS_SUCCESS)
                {
                    // Can't get Contact ID - skip this contact
                    continue;
                }
                
                USHORT contactId = (USHORT)contactIdValue;
                
                // Task 4.2: Contact tracking logic
                // Contact is active (touching) - TipSwitch is already confirmed above
                ContactInfo *contact = FindOrCreateContact(contactId);
                if (contact)
                {
                    contact->IsActive = true;
                    
                    // If Confidence=0, mark as rejected (palm)
                    // Once rejected, stays rejected until contact is released (Requirement 3.1, 3.2)
                    if (!hasConfidence)
                    {
                        contact->IsRejected = true;
                    }
                    
                    // Task 4.3: Count valid fingers (Confidence=1 AND not rejected)
                    // Requirement 2.1, 2.2, 2.4
                    if (!contact->IsRejected)
                    {
                        reportFingerCount++;
                    }
                }
            }
            
            // Update finger count from this report
            newFingerCount = reportFingerCount;
        }
        // Note: If ConfidenceSupported == false (fallback mode),
        // FingerCount will be set to ContactCount after the loop (Requirement 5.2)
    }
    
    // Update timestamp - we received a report from the touchpad
    g_Touchpad.LastContactTime = GetTickCount();
    
    // Update global state if we got valid data
    if (foundContactCount)
    {
        g_Touchpad.ContactCount = newContactCount;
        g_Touchpad.IsContactActive = (newContactCount > 0);
        
        // Set FingerCount based on whether Confidence is supported
        if (g_Touchpad.ConfidenceSupported && pHidP_GetUsages && sLinkCollectionCount > 0)
        {
            // Use calculated finger count from Confidence bit processing
            g_Touchpad.FingerCount = newFingerCount;
        }
        else
        {
            // Fallback: FingerCount = ContactCount (Requirement 5.2)
            g_Touchpad.FingerCount = newContactCount;
        }
        
        return true;
    }
    
    // Even if contact count wasn't found, receiving a report likely means contact is active
    // (touchpads typically only send reports when touched)
    if (!g_Touchpad.IsContactActive)
    {
        g_Touchpad.ContactCount = 1; // Assume at least 1 contact
        g_Touchpad.FingerCount = 1;  // Assume finger in fallback
        g_Touchpad.IsContactActive = true;
    }
    
    return true;
}
