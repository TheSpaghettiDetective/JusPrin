// Post a key press straight to a process, so a test can drive JusPrin without
// the screen (it works while the Mac is locked, where screenshot-based tools
// are refused). Keyboard events reach the app; mouse events do not.
//
//   swiftc -O -o mac_post_key tests/printer_sim/mac_post_key.swift
//   ./mac_post_key <pid> <keycode> [cmd] [shift]
//
// Keycodes: R=15 (Cmd+R slices), G=5 (Cmd+Shift+G opens the send dialog),
// Tab=48, Right=124, Space=49, Return=36, Escape=53.
import Foundation
import CoreGraphics

let arguments = CommandLine.arguments
guard arguments.count >= 3, let pid = pid_t(arguments[1]), let code = UInt16(arguments[2]) else {
    FileHandle.standardError.write("usage: mac_post_key <pid> <keycode> [cmd] [shift]\n".data(using: .utf8)!)
    exit(2)
}
var flags = CGEventFlags()
if arguments.contains("cmd") { flags.insert(.maskCommand) }
if arguments.contains("shift") { flags.insert(.maskShift) }
let source = CGEventSource(stateID: .hidSystemState)
for keyDown in [true, false] {
    let event = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(code), keyDown: keyDown)!
    event.flags = flags
    event.postToPid(pid)
    usleep(50_000)
}
