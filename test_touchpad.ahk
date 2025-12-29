; Touchpad Contact Test Script
; Shows the number of fingers touching the touchpad in a tooltip

#Requires AutoHotkey v2.0

; Update tooltip every 50ms
SetTimer(ShowTouchpadStatus, 50)

ShowTouchpadStatus() {
    count := A_TouchpadContact
    ToolTip("Touchpad Contact: " count)
}

; Press Escape to exit
Esc::ExitApp
