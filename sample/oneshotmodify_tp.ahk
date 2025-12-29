#Requires AutoHotkey v2.0
#SingleInstance Force

; ワンショットモディファイヤ: スペースキー
; - 単独押し: 通常のスペース
; - 長押し + 他キー: Ctrlモディファイヤとして機能

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

; スペース + キー → Ctrl + キー
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

; タッチパッド + キー → マウスクリック（ドラッグ対応）
; タッチパッドに指が触れている間、Fキーで左クリック/ドラッグ、Dキーで右クリック
#HotIf A_TouchpadContact > 0
f:: {
    Click "Down"  ; 左ボタン押下
    KeyWait "f"
    Click "Up"
}
d:: {
    Click "Right Down"  ; 右ボタン押下
    KeyWait "d"
    Click "Right Up"
}
w::Send "^w"           ; Ctrl+W
r::Send "^{PgDn}"      ; Ctrl+PageDown
e::Send "^{PgUp}"      ; Ctrl+PageUp
#HotIf
