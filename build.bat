@echo off

if not exist build mkdir build

set opts=-nologo -FC -std:c17 -Od -Oi -Zi
set code=%cd%
pushd build
cl %opts% %code%\png.c -Fepng.exe
popd
