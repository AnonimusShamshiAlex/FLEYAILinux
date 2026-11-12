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

cd /home/kali/Desktop/FLEYAILinux
bash build.sh --no-deps


sudo -u kali XDG_RUNTIME_DIR=/run/user/1000 pactl set-source-mute @DEFAULT_SOURCE@ 0
sudo -u kali XDG_RUNTIME_DIR=/run/user/1000 pactl set-source-volume @DEFAULT_SOURCE@ 100%


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


Чтобы включить и проверить микрофон внутри Kali Linux, выполните следующие команды в терминале по порядку.
(Важно: перед выполнением убедитесь, что в настройках самой виртуальной машины у вас уже включен аудиовход/микрофон).

1. Проверка, видит ли система звуковую карту

Выполните эту команду, чтобы убедиться, что виртуальная звуковая карта с поддержкой записи подключена:
bash
arecord -l
Use code with caution.
Если в ответ выведется список устройств (например, card 0: Intel...), значит система «видит» микрофон. Если пусто — нужно включить аудиовход в настройках VirtualBox/VMware.

2. Перезапуск звуковых служб

В Kali Linux иногда «зависает» аудиосервер. Перезапустите его в зависимости от вашей версии системы (выполните обе строки по очереди — сработает та, что установлена у вас):
bash
systemctl --user restart pipewire pipewire-pulse wireplumber
Use code with caution.
(Если выдаст ошибку, что служба не найдена, значит у вас старый аудиосервер, перезапустите его так):
bash
pulseaudio -k && pulseaudio --start
Use code with caution.

3. Включение микрофона и прибавление громкости через терминал

Часто микрофон в Kali по умолчанию выключен (находится в режиме Mute). Включите его и поставьте громкость на 100%:
bash
pactl set-source-mute @DEFAULT_SOURCE@ 0
pactl set-source-volume @DEFAULT_SOURCE@ 100%
Use code with caution.

4. Графический микшер (для наглядной проверки)

Если хочется проверить всё визуально и увидеть, «прыгает» ли полоска громкости, когда вы говорите, установите и запустите pavucontrol:
bash
sudo apt update && sudo apt install -y pavucontrol
pavucontrol
Use code with caution.
В открывшемся окне перейдите на вкладку Входные устройства (Input Devices) и посмотрите на индикатор под микрофоном.

5. Тестовая запись и прослушивание

Запишите 5 секунд голоса и сразу прослушайте результат, чтобы убедиться, что всё работает:
bash
arecord -d 5 test.wav && aplay test.wav
Use code with caution.
После проверки тестовый файл можно удалить командой rm test.wav.
Если на каком-то шаге терминал выдаст ошибку — напишите, какую именно, и мы разберемся!

┌──(root㉿kali)-[/home/kali/Desktop/FLEYAILinux/FLEY_AI_LINUX]
└─# arecord -l

**** List of CAPTURE Hardware Devices ****
card 0: I82801AAICH [Intel 82801AA-ICH], device 0: Intel ICH [Intel 82801AA-ICH]
  Subdevices: 0/1
  Subdevice #0: subdevice #0
card 0: I82801AAICH [Intel 82801AA-ICH], device 1: Intel ICH - MIC ADC [Intel 82801AA-ICH - MIC ADC]
  Subdevices: 1/1
  Subdevice #0: subdevice #0


Отлично, виртуальная машина «видит» микрофон — об этом говорит строка card 0, device 1: Intel ICH - MIC ADC. Однако по вашему приглашению терминала (root㉿kali) видно, что вы работаете под суперпользователем (root), а звуковые службы (PipeWire и PulseAudio) в Kali запускаются от имени обычного пользователя (kali) и из-под root по умолчанию заблокированы.
Чтобы микрофон заработал, выполните следующие шаги.

1. Включение микрофона от имени пользователя kali

Поскольку звук работает в сессии обычного пользователя, выполните настройку громкости и снятие беззвучного режима от его имени с помощью sudo -u kali:
bash
sudo -u kali pactl set-source-mute @DEFAULT_SOURCE@ 0
sudo -u kali pactl set-source-volume @DEFAULT_SOURCE@ 100%
Use code with caution.

2. Проверка микрофона через ALSA (работает и под root)

Вы можете напрямую проверить запись с устройства card 0, device 1 в обход звукового сервера командой:
bash
arecord -D plughw:0,1 -f cd -d 5 test.wav && aplay test.wav
Use code with caution.
• Что делает команда. Записывает 5 секунд звука напрямую с микрофонного входа (0,1) в формате CD-качества (-f cd) и сразу воспроизводит запись.
• Очистка. После проверки удалите тестовый файл командой rm test.wav.

3. Если вы запускаете Python-скрипт (например, FLEY_AI_LINUX) из-под root

Судя по пути /home/kali/Desktop/FLEYAILinux/FLEY_AI_LINUX, вы настраиваете голосового ассистента или скрипт распознавания речи. Если запускать его через root, библиотеки вроде PyAudio или SpeechRecognition часто не могут подключиться к звуковому серверу пользователя.
• Запускайте проект от имени kali. Переключитесь на обычного пользователя командой su - kali или выйдите из сессии root командой exit, а затем запустите ваш скрипт без sudo.
• Если нужен запуск строго от root. При запуске команды из-под root передавайте ему путь к аудиосерверу пользователя kali:
bash
XDG_RUNTIME_DIR=/run/user/1000 python3 ваш_скрипт.py
Use code with caution.
Появился ли звук при проверке командой arecord или нужна помощь с настройкой микрофона непосредственно внутри вашего проекта FLEY_AI_LINUX?


