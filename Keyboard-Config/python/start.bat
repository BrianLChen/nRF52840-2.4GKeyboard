@echo off
cd /d "%~dp0"
python keyboard_config.py
if errorlevel 1 pause
