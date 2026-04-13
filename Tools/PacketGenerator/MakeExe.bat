pushd %~dp0
pyinstaller --onefile --add-data "Templates;Templates" PacketGenerator.py
MOVE .\dist\PacketGenerator.exe .\GenPacket.exe
@RD /S /Q .\build
@RD /S /Q .\dist
DEL /S /F /Q .\PacketGenerator.spec
popd
