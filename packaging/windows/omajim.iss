; Instalador do Omajim para Windows (Inno Setup 6).
;
; Compilado pelo workflow windows.yml:
;   ISCC /DMyAppVersion=<versão> /DSourceDir=<pasta do pacote> /O<saída> omajim.iss
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\omajim"
#endif

[Setup]
; Identifica o programa para atualizações e para o desinstalador: não mudar.
AppId={{83F30C9D-9804-4B9E-93F9-990D42DD9F0B}
AppName=Omajim
AppVersion={#MyAppVersion}
AppPublisher=Higor Henrique
AppPublisherURL=https://github.com/higorhgon/omajim
AppSupportURL=https://github.com/higorhgon/omajim/issues
DefaultDirName={autopf}\Omajim
DisableProgramGroupPage=yes
; Em "Arquivos de Programas" por padrão (pede administrador); quem não tem
; permissão, ou prefere, escolhe instalar só para o próprio usuário.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
DisableDirPage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=omajim.ico
UninstallDisplayIcon={app}\omajim.exe
UninstallDisplayName=Omajim
LicenseFile=..\..\LICENSE
OutputBaseFilename=omajim-{#MyAppVersion}-windows-x64-setup
; Fecha o Omajim aberto (ele fica na bandeja) antes de atualizar os arquivos.
CloseApplications=yes

[Languages]
; O assistente abre no idioma do Windows quando houver tradução.
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
brazilianportuguese.ShortcutsGroup=Atalhos:
brazilianportuguese.StartMenuIcon=Criar um atalho no menu Iniciar
brazilianportuguese.DesktopIcon=Criar um atalho na área de trabalho
brazilianportuguese.AutoStart=Abrir o Omajim ao entrar no Windows
brazilianportuguese.LaunchAfter=Abrir o Omajim
english.ShortcutsGroup=Shortcuts:
english.StartMenuIcon=Create a Start menu shortcut
english.DesktopIcon=Create a desktop shortcut
english.AutoStart=Open Omajim when signing in to Windows
english.LaunchAfter=Launch Omajim

[Tasks]
; Cada atalho é uma opção do assistente, marcada por padrão e que pode ser
; desmarcada; abrir com o Windows vem desmarcado.
Name: "startmenuicon"; Description: "{cm:StartMenuIcon}"; GroupDescription: "{cm:ShortcutsGroup}"
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:ShortcutsGroup}"
Name: "autostart"; Description: "{cm:AutoStart}"; GroupDescription: "{cm:ShortcutsGroup}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Omajim"; Filename: "{app}\omajim.exe"; Tasks: startmenuicon
Name: "{autodesktop}\Omajim"; Filename: "{app}\omajim.exe"; Tasks: desktopicon
Name: "{autostartup}\Omajim"; Filename: "{app}\omajim.exe"; Tasks: autostart

[Run]
Filename: "{app}\omajim.exe"; Description: "{cm:LaunchAfter}"; Flags: nowait postinstall skipifsilent
; Uma atualização feita pelo próprio Omajim (/relaunch=1) o reabre no fim.
Filename: "{app}\omajim.exe"; Flags: nowait runasoriginaluser; Check: RelaunchRequested

[Code]
function RelaunchRequested: Boolean;
begin
  Result := ExpandConstant('{param:relaunch|0}') = '1';
end;
