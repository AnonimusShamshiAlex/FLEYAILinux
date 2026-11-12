#!/usr/bin/env bash
# FLEY AI для Linux: один скрипт - ставит зависимости, собирает программу, делает ярлык и запускает.
#   bash build.sh               собрать, установить и запустить
#   bash build.sh --no-deps     не ставить пакеты (если всё уже есть)
#   bash build.sh --no-run      не запускать после сборки
#   bash build.sh --uninstall   удалить программу и ярлыки (память и настройки остаются)
set -u
cd "$(dirname "$(readlink -f "$0")")"

APP=fley-ai
PREFIX="$HOME/.local/share/fley-ai"
BIN="$HOME/.local/bin"
CFG="$HOME/.config/fley-ai"
DESK="$HOME/.local/share/applications/fley-ai.desktop"

NODEPS=0; NORUN=0
for a in "$@"; do
  case "$a" in
    --no-deps) NODEPS=1 ;;
    --no-run) NORUN=1 ;;
    --uninstall)
      rm -f "$PREFIX/$APP" "$PREFIX/fley.png" "$BIN/$APP" "$DESK" "$HOME/.config/autostart/fley-ai.desktop"
      echo "FLEY AI удалён. Настройки ($CFG) и память ($PREFIX) оставлены."
      exit 0 ;;
  esac
done

say() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"

# ---------------------------------------------------------------- 1. зависимости
install_deps() {
  local need=0 c
  for c in g++ curl arecord espeak-ng xdotool pactl xdg-open; do command -v "$c" >/dev/null 2>&1 || need=1; done
  pkg-config --exists gtk+-3.0 2>/dev/null || need=1
  [ "$need" -eq 0 ] && return 0

  say "Ставлю нужные пакеты (попросит пароль sudo)"
  local mgr="" pk=() inst=()
  if command -v apt-get >/dev/null 2>&1; then
    mgr=apt
    $SUDO apt-get update
    inst=($SUDO apt-get install -y)
    pk=(g++ pkg-config libgtk-3-dev curl alsa-utils espeak-ng xdotool pulseaudio-utils playerctl xdg-utils xdg-user-dirs libglib2.0-bin libgtk-3-bin)
  elif command -v dnf >/dev/null 2>&1; then
    mgr=dnf
    inst=($SUDO dnf install -y)
    pk=(gcc-c++ pkgconf-pkg-config gtk3-devel curl alsa-utils espeak-ng xdotool pulseaudio-utils playerctl xdg-utils xdg-user-dirs glib2 gtk3)
  elif command -v pacman >/dev/null 2>&1; then
    mgr=pacman
    inst=($SUDO pacman -S --needed --noconfirm)
    pk=(base-devel pkgconf gtk3 curl alsa-utils espeak-ng xdotool libpulse playerctl xdg-utils xdg-user-dirs glib2)
  elif command -v zypper >/dev/null 2>&1; then
    mgr=zypper
    inst=($SUDO zypper --non-interactive install)
    pk=(gcc-c++ pkg-config gtk3-devel curl alsa-utils espeak-ng xdotool pulseaudio-utils playerctl xdg-utils xdg-user-dirs glib2-tools gtk3-tools)
  else
    echo "Не знаю ваш менеджер пакетов. Поставьте вручную: g++, pkg-config, GTK3 (dev), curl, alsa-utils, espeak-ng, xdotool, pulseaudio-utils, playerctl."
    return 0
  fi
  # по одному, чтобы отсутствие необязательного пакета (например playerctl) ничего не ломало
  local p
  for p in "${pk[@]}"; do "${inst[@]}" "$p" || echo "  (пропускаю $p)"; done
}
[ "$NODEPS" -eq 0 ] && install_deps

command -v g++ >/dev/null 2>&1 || { echo "Не найден g++. Установите компилятор (sudo apt install g++) и запустите снова."; exit 1; }
pkg-config --exists gtk+-3.0 2>/dev/null || { echo "Не найден GTK3 для разработки (sudo apt install libgtk-3-dev) - запустите снова."; exit 1; }

# ---------------------------------------------------------------- 2. сборка
say "Собираю FLEY AI"
mkdir -p build
g++ -std=c++17 -O2 -pthread -Wno-unused-function -Wno-deprecated-declarations -Isrc src/main_linux.cpp -o "build/$APP" \
    $(pkg-config --cflags --libs gtk+-3.0) || { echo "Ошибка сборки. Пришлите текст ошибки выше - исправлю."; exit 1; }

# ---------------------------------------------------------------- 3. установка
say "Устанавливаю"
mkdir -p "$PREFIX" "$BIN" "$CFG" "$(dirname "$DESK")"
cp -f "build/$APP" "$PREFIX/$APP"
cp -f assets/fley.png "$PREFIX/fley.png"
ln -sf "$PREFIX/$APP" "$BIN/$APP"
[ -f "$CFG/.env" ] || cp .env.example "$CFG/.env"

cat > "$DESK" <<EOF
[Desktop Entry]
Type=Application
Name=FLEY AI
Comment=Голосовой ИИ-помощник
Exec=$PREFIX/$APP
Icon=$PREFIX/fley.png
Terminal=false
Categories=Utility;AudioVideo;
EOF
chmod +x "$DESK"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(dirname "$DESK")" >/dev/null 2>&1
if command -v xdg-user-dir >/dev/null 2>&1; then
  D="$(xdg-user-dir DESKTOP 2>/dev/null)"
  if [ -d "$D" ]; then cp -f "$DESK" "$D/FLEY AI.desktop" && chmod +x "$D/FLEY AI.desktop"; fi
fi

echo
echo "Готово. Программа: $PREFIX/$APP (команда: fley-ai, ярлык FLEY AI в меню приложений)."
echo "Настройки и ключ: $CFG/.env (или кнопка «Настройки» в окне)."
[ "${XDG_SESSION_TYPE:-}" = "wayland" ] && echo "Вы на Wayland: нажатие клавиш и набор текста (xdotool) работают не везде. Для полной работы выберите сеанс «Xorg» при входе."

# ---------------------------------------------------------------- 4. запуск
if [ "$NORUN" -eq 0 ]; then
  say "Запускаю FLEY AI"
  exec "$PREFIX/$APP"
fi
