#Requires AutoHotkey v2.0
#SingleInstance Force

; ============================================================
; IME制御関数 (alt-ime-ahk方式 v2変換)
; ============================================================
IME_SET(SetSts, WinTitle := "A") {
    hwnd := WinGetID(WinTitle)
    if (WinActive(WinTitle)) {
        ptrSize := A_PtrSize ? A_PtrSize : 4
        cbSize := 4 + 4 + (ptrSize * 6) + 16
        stGTI := Buffer(cbSize, 0)
        NumPut("UInt", cbSize, stGTI, 0)
        if DllCall("GetGUIThreadInfo", "UInt", 0, "Ptr", stGTI)
            hwnd := NumGet(stGTI, 8 + ptrSize, "Ptr")
    }
    return DllCall("SendMessage"
        , "Ptr", DllCall("imm32\ImmGetDefaultIMEWnd", "Ptr", hwnd, "Ptr")
        , "UInt", 0x0283  ; WM_IME_CONTROL
        , "Ptr", 0x006    ; IMC_SETOPENSTATUS
        , "Ptr", SetSts)
}

; ============================================================
; 主要キーをHotKeyに設定し、何もせずパススルー
; (これによりA_PriorHotkeyが正しく設定される)
; 注意: d, e, f, r, w はタッチパッド条件で使うため除外
; ============================================================
*~a::
*~b::
*~c::
*~g::
*~h::
*~i::
*~j::
*~k::
*~l::
*~m::
*~n::
*~o::
*~p::
*~q::
*~s::
*~t::
*~u::
*~v::
*~x::
*~y::
*~z::
*~1::
*~2::
*~3::
*~4::
*~5::
*~6::
*~7::
*~8::
*~9::
*~0::
*~F1::
*~F2::
*~F3::
*~F4::
*~F5::
*~F6::
*~F7::
*~F8::
*~F9::
*~F10::
*~F11::
*~F12::
*~Esc::
*~Tab::
*~Enter::
*~Delete::
*~Home::
*~End::
*~PgUp::
*~PgDn::
*~Left::
*~Right::
*~Up::
*~Down::
*~Backspace::
*~Insert::
*~PrintScreen::
*~`::
*~-::
*~=::
*~[::
*~]::
*~\::
*~;::
*~'::
*~,::
*~.::
*~/::
*~Numpad0::
*~Numpad1::
*~Numpad2::
*~Numpad3::
*~Numpad4::
*~Numpad5::
*~Numpad6::
*~Numpad7::
*~Numpad8::
*~Numpad9::
*~NumpadDot::
*~NumpadDiv::
*~NumpadMult::
*~NumpadAdd::
*~NumpadSub::
*~NumpadEnter::
{
    return
}

; ============================================================
; タッチパッドで使うキー (d, e, f, r, w) のパススルー定義
; タッチパッド非接触時はパススルー、接触時は後続の#HotIfで処理
; ============================================================
#HotIf A_TouchpadContact = 0
*~d::
*~e::
*~f::
*~r::
*~w::
{
    return
}
#HotIf

; ============================================================
; ALT/Shiftキー押下時: ダミーキー送信でA_PriorHotkey設定
; ALTはメニュー遷移抑制も兼ねる
; ============================================================
*~LAlt::Send "{Blind}{vk07}"
*~RAlt::Send "{Blind}{vk07}"
*~LShift::Send "{Blind}{vk07}"
*~RShift::Send "{Blind}{vk07}"

; ============================================================
; ALT/Shiftキー単独押し（空打ち）でIME切り替え
; 左ALT/左Shift: IME OFF
; 右ALT/右Shift: IME ON
; ============================================================
LAlt up::{
    if (A_PriorHotkey = "*~LAlt")
        IME_SET(0)
}

RAlt up::{
    if (A_PriorHotkey = "*~RAlt")
        IME_SET(1)
}

LShift up::{
    if (A_PriorHotkey = "*~LShift")
        IME_SET(0)
}

RShift up::{
    if (A_PriorHotkey = "*~RShift")
        IME_SET(1)
}

; ============================================================
; ワンショットモディファイヤ: スペースキー
; - 単独押し: 通常のスペース
; - 長押し + 他キー: Ctrlモディファイヤとして機能
; ============================================================
global spaceDown := false
global spaceUsedAsModifier := false

*Space:: {
    global spaceDown, spaceUsedAsModifier
    spaceDown := true
    spaceUsedAsModifier := false
}

*Space up:: {
    global spaceDown, spaceUsedAsModifier
    spaceDown := false
    if (!spaceUsedAsModifier) {
        Send "{Space}"
    }
}

#HotIf spaceDown
*w:: {
    global spaceUsedAsModifier
    spaceUsedAsModifier := true
    Send "^w"
}

*l:: {
    global spaceUsedAsModifier
    spaceUsedAsModifier := true
    Send "^l"
}

*t:: {
    global spaceUsedAsModifier
    spaceUsedAsModifier := true
    Send "^t"
}
#HotIf

; ============================================================
; タッチパッド + キー → マウスクリック/ショートカット
; タッチパッドに指が触れている間のみ有効（パーム除外）
; ============================================================
#HotIf A_TouchpadContact > 0
f:: {
    Click "Down"
    KeyWait "f"
    Click "Up"
}
d:: {
    Click "Right Down"
    KeyWait "d"
    Click "Right Up"
}
w::Send "^w"
r::Send "^{PgDn}"
e::Send "^{PgUp}"
#HotIf
