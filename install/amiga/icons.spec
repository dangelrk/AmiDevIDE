# Workbench-Icons der Installationsschublade (tools/release/amiga_release.py),
# Format siehe tools/release/mkicon.py. Positionen: Mitte des Icons minus 18.
.                          drawer  drawer    win=60,30,500,200
AmigaIDE                   drawer  drawer    win=80,50,300,110 pos=42,6
Install-AmiDevIDE          project install   tool=Installer tt=APPNAME=AmigaIDE tt=MINUSER=AVERAGE tt=DEFUSER=AVERAGE pos=152,6
Uninstall-AmiDevIDE        project uninstall tool=C:IconX tt=WINDOW=CON:0/20/640/200/Uninstall-AmiDevIDE/CLOSE/WAIT pos=282,6
ReadMe                     project doc       tool=SYS:Utilities/MultiView pos=402,6
LICENSE                    project doc       tool=SYS:Utilities/MultiView pos=42,70
THIRD-PARTY-NOTICES        project doc       tool=SYS:Utilities/MultiView pos=172,70
Install-AmiDevIDE.shell    project doc       tool=C:IconX tt=WINDOW=CON:0/20/640/300/Install-AmiDevIDE/CLOSE/WAIT pos=332,70

AmigaIDE/AmigaIDE          tool    ide       stack=65536 pos=42,6
AmigaIDE/AmigaIDE.guide    project guide     tool=SYS:Utilities/MultiView pos=152,6
