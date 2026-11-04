@echo off
set BACKUP_DIR=backups\%DATE:~6,4%-%DATE:~3,2%-%DATE:~0,2%
mkdir "%BACKUP_DIR%" 2>nul
scp lopag@zuluserver.ddns.net:/home/lopag/Philanthropy-System/server/philanthropy_arena.db "%BACKUP_DIR%\"
scp -r lopag@zuluserver.ddns.net:/home/lopag/Philanthropy-System/server/static/uploads "%BACKUP_DIR%\uploads"
echo Backup salvato in %BACKUP_DIR%!
pause