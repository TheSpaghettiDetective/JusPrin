; Included near the top of the NSIS installer script CPack writes
; (packaging.cmake adds it through CPACK_NSIS_DEFINES).

; The finish page's "Run JusPrin" box. The installer runs as administrator, and
; a program it starts directly would too: Windows then blocks files dragged
; from File Explorer into it. Explorer starts JusPrin as the signed-in user.
!define MUI_FINISHPAGE_RUN_FUNCTION JusPrinRun
Function JusPrinRun
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\orca-slicer.exe"'
FunctionEnd
