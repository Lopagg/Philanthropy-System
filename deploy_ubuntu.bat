@echo off
set SERVER_USER=lopag
set SERVER_IP=zuluserver.ddns.net
set REMOTE_DIR=/home/lopag/Philanthropy-System

echo [1/2] Aggiornamento codice da GitHub e riavvio servizio su %SERVER_IP%...
ssh %SERVER_USER%@%SERVER_IP% "cd %REMOTE_DIR% && git pull origin main && sudo systemctl restart controlpanel"

echo.
echo [2/2] Verifica stato servizio:
ssh %SERVER_USER%@%SERVER_IP% "systemctl is-active controlpanel"

echo.
echo === DEPLOY PHILANTHROPY COMPLETATO ===
pause