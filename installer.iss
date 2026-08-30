; ============================================================
; Sound Vault（声库）v1.4.0 安装程序 — Inno Setup 7
; - 默认安装到 C:\Program Files\Sound Vault（管理员权限，可改位置）
; - 简体中文界面（Unicode）
; - 开始菜单 + 桌面快捷方式
; - 完整卸载：程序文件 / 快捷方式 / HKLM 注册表
; - 用户数据（标签/收藏/波形缓存）位于 %APPDATA%\SoundVault，卸载不删除
; ============================================================

#define AppName "Sound Vault"
#define AppExe "ShengKu.exe"
#define AppVer "1.4.0"
#define AppPublisher "Sound Vault Project"
#define AppId "SoundVault.Desktop"

[Setup]
AppId={{F2D4A1B8-7C3E-4E9A-9B6D-2A5E8C1F4D77}
AppName={#AppName}
AppVersion={#AppVer}
AppPublisher={#AppPublisher}
AppVerName={#AppName} {#AppVer}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=D:\fileMKlink\代码软件\声库-v0.9\dist
OutputBaseFilename=SoundVault-Setup-v1.4.0
SetupIconFile=D:\fileMKlink\代码软件\声库-v0.9\src\assets\app.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=*.exe
MinVersion=10.0
Uninstallable=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式(&D)"; GroupDescription: "附加任务："; Flags: checkedonce

[Files]
Source: "D:\fileMKlink\代码软件\声库-v0.9\dist\Sound Vault\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "立即运行 {#AppName}(&R)"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
  begin
    { 关闭正在运行的实例，避免文件占用导致安装失败 }
    Exec('taskkill.exe', '/IM {#AppExe} /F', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
