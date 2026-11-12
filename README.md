# FLEY AI для Linux
FLEY AI-Voce is an AI voice assistant that not only talks to you but can also control your PC.


Тот же FLEY AI, но под Linux.
FLEY AI-Voce — голосовой AI-ассистент, который не только будет с вами разговаривать, но и сможет управлять вашим ПК: открывать ссылки, сайты и программы, создавать файлы, презентации, писать код и даже выключать и включать ПК, работать с принтером и делать практически что угодно.

https://fleyaidownload.netlify.app/

## Запуск (один скрипт)

```bash
unzip FLEY_AI_LINUX.zip && cd FLEY_AI_LINUX
bash build.sh

# Если ошибка то вот это 

cd /home/kali/Desktop/FLEYAILinux
for n in state keyErr remFlag online stopSpeak closing clearReq taint ensurePlay reloadReq showSet level1000 factCount mx status model statusErr logv log set hist dir envPath confirmFn facts playPid micFd micPid noiseFloor micGain remMx remDue win area t lv cpu ram full cpuTot cpuIdle; do
  sed -i "s/\bg_$n\b/fl_$n/g" src/engine.hpp src/main_linux.cpp
done
grep -c "g_log" src/engine.hpp src/main_linux.cpp
bash build.sh --no-deps

```

Скрипт сам: поставит пакеты (apt / dnf / pacman / zypper), соберёт программу, создаст ярлык «FLEY AI» и запустит окно.
Если ключа ещё нет, окно настроек откроется само: вставьте ключ Groq (бесплатно: console.groq.com/keys).

Дальше запускайте ярлыком «FLEY AI» из меню приложений или командой `fley-ai`.
Флаги: `--no-deps` (не ставить пакеты), `--no-run` (не запускать), `--uninstall` (удалить).

## Управление
Пробел или клик по ядру: микрофон вкл/выкл (во время речи FLEY - прервать). F11: полный экран. Ctrl+Q: выход.

## Где что лежит
- Настройки и ключ: `~/.config/fley-ai/.env`
- Память, история, лог: `~/.local/share/fley-ai/`

## Что используется
curl (запросы), arecord (микрофон), espeak-ng (локальный голос), paplay/aplay (звук), xdotool (клавиши и ввод),
pactl (громкость), playerctl (медиа), xdg-open (ссылки и папки), gio trash (корзина).

## Wayland
Нажатие клавиш и набор текста (`xdotool`) на Wayland работают только в части приложений.
Для полной работы выберите сеанс «Xorg» на экране входа. Остальное (голос, ИИ, программы, YouTube, файлы) работает везде.

## Если что-то не работает
- Нет голоса: `sudo apt install espeak-ng pulseaudio-utils`
- Не слышит микрофон: `arecord -l`, затем укажите устройство в `FLEY_MIC_DEVICE`
- Лог ошибок: `~/.local/share/fley-ai/fley.log`

Я не могу дать свой API, потому что сервис меня заблокирует за превышение лимитов. Но ты можешь прямо сейчас за 1 минуту получить свой собственный бесплатный ключ вот здесь: https://console.groq.com/keys и вставить в .env GROQ_API_KEY=<Таое API>
