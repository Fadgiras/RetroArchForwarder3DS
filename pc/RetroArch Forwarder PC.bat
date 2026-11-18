@echo off
rem Abre RetroArch Forwarder PC (PowerShell, no necesita instalar nada)
start "" powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0RetroArchForwarderPC.ps1"
