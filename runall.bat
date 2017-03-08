@echo off
setlocal enabledelayedexpansion
set string=rpmalloc,tcmalloc,crt,hoard,nedmalloc
:again
for /f "tokens=1* delims=," %%x in ("%string%") do (
	set executable=bin\windows\release\x86-64\benchmark-%%x.exe
	set string=%%y
)

%executable% 1 0 2 16 16352
%executable% 2 0 2 16 16352
%executable% 3 0 2 16 16352
%executable% 4 0 2 16 16352
%executable% 5 0 2 16 16352
%executable% 6 0 2 16 16352
%executable% 7 0 2 16 16352
%executable% 8 0 2 16 16352
%executable% 9 0 2 16 16352
%executable% 10 0 2 16 16352

if not ".%string%"=="." goto again
