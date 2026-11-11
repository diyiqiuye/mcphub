Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

!define APPNAME     "MCP Hub"
!define APPDESC     "MCP 服务桌面控制台（工具体检 / 下载安装 / 网关）"
!define APPEXE      "mcphub.exe"
!define REGKEY      "Software\MCPHub"
!define UNINSTKEY   "Software\Microsoft\Windows\CurrentVersion\Uninstall\MCPHub"

Name "${APPNAME} ${VERSION}"
BrandingText "${APPNAME} ${VERSION}"
OutFile "..\dist\MCPHub-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\MCPHub"
InstallDirRegKey HKCU "${REGKEY}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 64
SetDatablockOptimize on

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName"     "${APPNAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "${APPNAME} 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion"     "${VERSION}"
VIAddVersionKey /LANG=2052 "ProductVersion"  "${VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright"  "mcphub"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\${APPEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "立即运行 MCP Hub"
!define MUI_FINISHPAGE_NOREBOOTSUPPORT

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

LangString DESC_Main    ${LANG_SIMPCHINESE} "MCP Hub 主程序：mcphub.exe（控制台）、mcphubd.exe（守护进程 / MCP 网关）、工具目录与配置。"
LangString DESC_Main    ${LANG_ENGLISH}     "MCP Hub core: console, daemon / MCP gateway, catalog and config."
LangString DESC_Desktop ${LANG_SIMPCHINESE} "在桌面创建 MCP Hub 快捷方式。"
LangString DESC_Desktop ${LANG_ENGLISH}     "Create a desktop shortcut."

!macro StopRunning
  DetailPrint "关闭正在运行的 MCP Hub…"
  nsExec::Exec 'taskkill /IM ${APPEXE} /F'
  Pop $0
  nsExec::Exec 'taskkill /IM mcphubd.exe /F'
  Pop $0
  Sleep 400
!macroend

Section "MCP Hub 主程序（必需）" SecMain
  SectionIn RO
  SetShellVarContext current
  !insertmacro StopRunning

  SetOutPath "$INSTDIR"
  File /r "..\stage\*.*"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\MCP Hub"
  CreateShortcut "$SMPROGRAMS\MCP Hub\MCP Hub.lnk" "$INSTDIR\bin\${APPEXE}" "" "$INSTDIR\bin\${APPEXE}" 0
  CreateShortcut "$SMPROGRAMS\MCP Hub\卸载 MCP Hub.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "Version" "${VERSION}"

  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName"     "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher"       "mcphub"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon"     "$INSTDIR\bin\${APPEXE}"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr HKCU "${UNINSTKEY}" "Comments"        "${APPDESC}"
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

Section "桌面快捷方式" SecDesktop
  SetShellVarContext current
  CreateShortcut "$DESKTOP\MCP Hub.lnk" "$INSTDIR\bin\${APPEXE}" "" "$INSTDIR\bin\${APPEXE}" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}    $(DESC_Main)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  SetShellVarContext current
FunctionEnd

Section "Uninstall"
  SetShellVarContext current
  !insertmacro StopRunning

  Delete "$DESKTOP\MCP Hub.lnk"
  RMDir /r "$SMPROGRAMS\MCP Hub"

  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\catalog"
  Delete "$INSTDIR\bundle.json"
  Delete "$INSTDIR\ida-tools.json"
  Delete "$INSTDIR\run.cmd"
  Delete "$INSTDIR\README.md"

  IfSilent keepData
  MessageBox MB_YESNO|MB_ICONQUESTION "是否同时删除已下载的工具（tools）以及配置与缓存（state、cache）？" IDNO keepData
    RMDir /r "$INSTDIR\tools"
    RMDir /r "$INSTDIR\state"
    RMDir /r "$INSTDIR\cache"
  keepData:

  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "${REGKEY}"
SectionEnd
