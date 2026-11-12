// FLEY AI для Linux - окно (GTK3 + Cairo). Вся логика - в engine.hpp
#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include <cmath>

#include "engine.hpp"

static GtkWidget *g_win = nullptr, *g_area = nullptr;
static double g_t = 0, g_lv = 0, g_cpu = 0, g_ram = 0;
static bool g_full = false;

struct C3 { double r, g, b; };
static const C3 C_CYAN = {0.12, 0.78, 1.0}, C_ICE = {0.6, 0.92, 1.0}, C_AMBER = {1.0, 0.69, 0.19}, C_RED = {1.0, 0.35, 0.3}, C_DIM = {0.25, 0.4, 0.55};

static C3 StateColor(int st, bool online) {
    if (!online) return C_DIM;
    if (st == THINKING) return C_AMBER;
    if (st == SPEAKING) return C_ICE;
    return C_CYAN;
}

// Рисует (или только измеряет) текст. align: 0 слева, 1 по центру, 2 справа. Возвращает высоту.
static int Text(cairo_t* cr, const std::string& s, double x, double y, double w, double size, C3 c, double alpha, int align = 0,
                bool bold = false, bool mono = false, bool draw = true) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_new();
    pango_font_description_set_family(fd, mono ? "Monospace" : "Sans");
    pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, s.c_str(), -1);
    if (w > 0) {
        pango_layout_set_width(l, (int)(w * PANGO_SCALE));
        pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    }
    pango_layout_set_alignment(l, align == 1 ? PANGO_ALIGN_CENTER : align == 2 ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(l, &tw, &th);
    if (draw) {
        cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, l);
    }
    pango_font_description_free(fd);
    g_object_unref(l);
    return th;
}

struct Geo { double cx, cy, R, s; };
static Geo Geometry(int W, int H) {
    double s = std::max(0.55, std::min(1.5, std::min(W / 1000.0, H / 700.0)));
    return {W / 2.0, H * 0.36, 105.0 * s, s};
}

static void Ring(cairo_t* cr, double cx, double cy, double r, C3 c, double a, double lw, double dash, double off) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
    cairo_set_line_width(cr, lw);
    if (dash > 0) { double d[2] = {dash, dash * 1.2}; cairo_set_dash(cr, d, 2, off); }
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void DrawOrb(cairo_t* cr, const Geo& g, int st, bool on) {
    C3 c = StateColor(st, on);
    double lv = g_lv, R = g.R * (1.0 + 0.10 * lv);
    // свечение
    cairo_pattern_t* gl = cairo_pattern_create_radial(g.cx, g.cy, R * 0.3, g.cx, g.cy, R * 2.3);
    cairo_pattern_add_color_stop_rgba(gl, 0, c.r, c.g, c.b, 0.28 + 0.2 * lv);
    cairo_pattern_add_color_stop_rgba(gl, 1, c.r, c.g, c.b, 0.0);
    cairo_set_source(cr, gl);
    cairo_arc(cr, g.cx, g.cy, R * 2.3, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(gl);
    // кольца
    Ring(cr, g.cx, g.cy, R * 1.55, c, 0.35, 1.5 * g.s, 14 * g.s, g_t * 30);
    Ring(cr, g.cx, g.cy, R * 1.32, c, 0.55, 2.0 * g.s, 40 * g.s, -g_t * 45);
    Ring(cr, g.cx, g.cy, R * 1.12, c, 0.9, 1.5 * g.s, 0, 0);
    // лучи вокруг ядра: реагируют на голос, при раздумьях - вращающийся «радар»
    const int N = 72;
    cairo_save(cr);
    cairo_set_line_width(cr, 2.2 * g.s);
    for (int i = 0; i < N; ++i) {
        double a = i * 2 * M_PI / N + g_t * 0.12, amp;
        if (st == THINKING) { double k = std::max(0.0, std::cos(a * 1.0 - g_t * 3.2)); amp = 0.12 + 0.88 * std::pow(k, 6); }
        else if (st == SPEAKING) amp = 0.2 + 0.6 * std::fabs(std::sin(g_t * 5 + i * 0.7) * std::cos(g_t * 1.7 + i * 0.25));
        else amp = 0.1 + lv * 0.9 * (0.4 + 0.6 * std::fabs(std::sin(g_t * 4 + i * 0.9)));
        double r0 = R * 1.18, r1 = r0 + R * (0.05 + 0.32 * amp);
        cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.35 + 0.6 * amp);
        cairo_move_to(cr, g.cx + std::cos(a) * r0, g.cy + std::sin(a) * r0);
        cairo_line_to(cr, g.cx + std::cos(a) * r1, g.cy + std::sin(a) * r1);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
    // ядро
    cairo_pattern_t* core = cairo_pattern_create_radial(g.cx - R * 0.2, g.cy - R * 0.25, R * 0.05, g.cx, g.cy, R);
    cairo_pattern_add_color_stop_rgba(core, 0, 1, 1, 1, on ? 0.95 : 0.4);
    cairo_pattern_add_color_stop_rgba(core, 0.45, c.r, c.g, c.b, on ? 0.85 : 0.35);
    cairo_pattern_add_color_stop_rgba(core, 1, c.r * 0.2, c.g * 0.2, c.b * 0.3, 0.9);
    cairo_set_source(cr, core);
    cairo_arc(cr, g.cx, g.cy, R * 0.92, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(core);
    Ring(cr, g.cx, g.cy, R * 0.92, C_ICE, 0.8, 2.0 * g.s, 0, 0);
}

static void DrawLog(cairo_t* cr, double x, double top, double w, double bottom) {
    std::vector<LogEntry> es;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        size_t from = g_log.size() > 40 ? g_log.size() - 40 : 0;
        es.assign(g_log.begin() + (long)from, g_log.end());
    }
    cairo_set_source_rgba(cr, 0.02, 0.08, 0.14, 0.55);
    cairo_rectangle(cr, x, top, w, bottom - top);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, C_CYAN.r, C_CYAN.g, C_CYAN.b, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, x + 0.5, top + 0.5, w - 1, bottom - top - 1);
    cairo_stroke(cr);
    cairo_save(cr);
    cairo_rectangle(cr, x, top, w, bottom - top);
    cairo_clip(cr);
    double y = bottom - 10;
    for (size_t i = es.size(); i-- > 0 && y > top;) {
        const LogEntry& e = es[i];
        C3 col = e.role == 0 ? C_ICE : e.role == 1 ? C_CYAN : C_AMBER;
        std::string pre = e.role == 0 ? "ВЫ   " : e.role == 1 ? "FLEY  " : "›  ";
        std::string line = pre + e.text;
        int h = Text(cr, line, 0, 0, w - 24, 15, col, 1, 0, false, false, false);
        y -= h + 6;
        Text(cr, line, x + 12, y, w - 24, 15, col, e.role == 2 ? 0.8 : 1.0);
    }
    cairo_restore(cr);
}

static gboolean OnDraw(GtkWidget* w, cairo_t* cr, gpointer) {
    int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    int st = g_state;
    bool on = g_online;
    Geo g = Geometry(W, H);
    // фон
    cairo_pattern_t* bg = cairo_pattern_create_radial(W / 2.0, H * 0.36, 10, W / 2.0, H * 0.36, std::max(W, H) * 0.85);
    cairo_pattern_add_color_stop_rgb(bg, 0, 0.04, 0.10, 0.18);
    cairo_pattern_add_color_stop_rgb(bg, 1, 0.01, 0.02, 0.05);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);
    cairo_set_source_rgba(cr, 0.12, 0.45, 0.65, 0.07);
    cairo_set_line_width(cr, 1);
    for (int x = 0; x < W; x += 48) { cairo_move_to(cr, x + 0.5, 0); cairo_line_to(cr, x + 0.5, H); }
    for (int y = 0; y < H; y += 48) { cairo_move_to(cr, 0, y + 0.5); cairo_line_to(cr, W, y + 0.5); }
    cairo_stroke(cr);

    // шапка
    Text(cr, "F L E Y   A I", 24, 16, 0, 22 * std::max(0.8, g.s), C_CYAN, 1, 0, true);
    std::string model;
    { std::lock_guard<std::mutex> lk(g_mx); model = g_model; }
    Text(cr, "FLEY Industries  ·  " + model, 24, 16 + 28 * std::max(0.8, g.s), 0, 12, C_CYAN, 0.55, 0, false, true);
    time_t t = time(nullptr);
    char clk[16];
    strftime(clk, sizeof clk, "%H:%M:%S", localtime(&t));
    // статистика
    char buf[96];
    snprintf(buf, sizeof buf, "CPU %3.0f%%   RAM %3.0f%%   ПАМЯТЬ %d", g_cpu, g_ram, (int)g_factCount);
    Text(cr, buf, 24, 16 + 50 * std::max(0.8, g.s), 0, 12, C_ICE, 0.7, 0, false, true);

    DrawOrb(cr, g, st, on);

    // статус под ядром
    std::string status;
    bool err;
    { std::lock_guard<std::mutex> lk(g_mx); status = g_status; err = g_statusErr; }
    double sy = g.cy + g.R * 1.95;
    int sh = Text(cr, status, W * 0.15, sy, W * 0.7, 16 * std::max(0.85, g.s), err ? C_RED : StateColor(st, on), 0.95, 1);

    // журнал диалога
    double lw = std::min((double)W - 48, 920.0), lx = (W - lw) / 2, ltop = sy + sh + 14, lbot = H - 34;
    if (lbot - ltop > 60) DrawLog(cr, lx, ltop, lw, lbot);
    Text(cr, "Пробел - микрофон   F11 - на весь экран   Esc - выйти из полного экрана   Ctrl+Q - выход", 0, H - 24, W - 16, 11, C_CYAN, 0.45, 2, false, true);
    Text(cr, clk, 0, 16, W - 24, 22 * std::max(0.8, g.s), C_CYAN, 0.9, 2, false, true);
    return FALSE;
}

static void OnOrbClick() {
    if (g_state == SPEAKING) { g_stopSpeak = 1; return; }
    g_online = g_online ? 0 : 1;
}
static gboolean OnPress(GtkWidget* w, GdkEventButton* ev, gpointer) {
    if (ev->button != 1) return FALSE;
    Geo g = Geometry(gtk_widget_get_allocated_width(w), gtk_widget_get_allocated_height(w));
    double dx = ev->x - g.cx, dy = ev->y - g.cy;
    if (std::sqrt(dx * dx + dy * dy) < g.R * 1.3) OnOrbClick();
    return TRUE;
}

static void ToggleFull() {
    g_full = !g_full;
    if (g_full) gtk_window_fullscreen(GTK_WINDOW(g_win)); else gtk_window_unfullscreen(GTK_WINDOW(g_win));
}
static void QuitApp() {
    g_closing = 1;
    KillMicNow();
    pid_t p = g_playPid;
    if (p > 0) kill(p, SIGTERM);
    gtk_main_quit();
}
static gboolean OnKey(GtkWidget*, GdkEventKey* ev, gpointer) {
    if (ev->keyval == GDK_KEY_space) { OnOrbClick(); return TRUE; }
    if (ev->keyval == GDK_KEY_F11) { ToggleFull(); return TRUE; }
    if (ev->keyval == GDK_KEY_Escape && g_full) { ToggleFull(); return TRUE; }
    if ((ev->state & GDK_CONTROL_MASK) && (ev->keyval == GDK_KEY_q || ev->keyval == GDK_KEY_Q)) { QuitApp(); return TRUE; }
    return FALSE;
}

// ------------------------------------------------------------------ окно настроек
static void OpenSettings() {
    static bool open = false;
    if (open) return;
    open = true;
    GtkWidget* d = gtk_dialog_new_with_buttons("Настройки FLEY AI", GTK_WINDOW(g_win), GTK_DIALOG_MODAL, "Отмена", GTK_RESPONSE_CANCEL, "Сохранить",
                                               GTK_RESPONSE_OK, nullptr);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    auto row = [&](int r, const char* label, const std::string& val, bool secret) {
        GtkWidget* l = gtk_label_new(label);
        gtk_widget_set_halign(l, GTK_ALIGN_END);
        GtkWidget* e = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(e), val.c_str());
        gtk_entry_set_width_chars(GTK_ENTRY(e), 44);
        if (secret) gtk_entry_set_visibility(GTK_ENTRY(e), FALSE);
        gtk_grid_attach(GTK_GRID(grid), l, 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), e, 1, r, 1, 1);
        return e;
    };
    std::string env = ReadFileS(g_envPath);
    auto cur = parse_env(env);
    auto cv = [&](const char* k) { auto it = cur.find(k); return it == cur.end() ? std::string() : it->second; };
    GtkWidget* eKey = row(0, "API-ключ (Groq / OpenRouter / OpenAI)", g_set.key, true);
    GtkWidget* eCall = row(1, "Как к вам обращаться", cv("FLEY_CALL_ME"), false);
    GtkWidget* eWake = row(2, "Слово-активатор (пусто = на всё)", cv("FLEY_WAKE"), false);
    GtkWidget* eSil = row(3, "Пауза до конца фразы, секунд", cv("FLEY_SILENCE_SECONDS"), false);
    GtkWidget* chk = gtk_check_button_new_with_label("Спрашивать подтверждение для опасных действий");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), g_set.confirm);
    gtk_grid_attach(GTK_GRID(grid), chk, 1, 4, 1, 1);
    GtkWidget* hint = gtk_label_new("Бесплатный ключ Groq: console.groq.com/keys. Остальные настройки - в файле .env");
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 5, 2, 1);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), grid);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        std::string key = trim(gtk_entry_get_text(GTK_ENTRY(eKey)));
        if (!key.empty()) {
            std::string name = starts_with(key, "sk-or-") ? "OPENROUTER_API_KEY" : starts_with(key, "gsk_") ? "GROQ_API_KEY" : "OPENAI_API_KEY";
            for (const char* n : {"OPENAI_API_KEY", "OPENROUTER_API_KEY", "GROQ_API_KEY"})
                if (name != n) env = set_env_value(env, n, "", false);
            env = set_env_value(env, name, key, true);
        }
        env = set_env_value(env, "FLEY_CALL_ME", trim(gtk_entry_get_text(GTK_ENTRY(eCall))), true);
        env = set_env_value(env, "FLEY_WAKE", trim(gtk_entry_get_text(GTK_ENTRY(eWake))), true);
        std::string sil = trim(gtk_entry_get_text(GTK_ENTRY(eSil)));
        if (!sil.empty()) env = set_env_value(env, "FLEY_SILENCE_SECONDS", sil, true);
        env = set_env_value(env, "FLEY_CONFIRM", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk)) ? "1" : "0", true);
        WriteFileS(g_envPath, env);
        g_keyErr = 0;
        g_reloadReq = 1;
        SetStatus("Настройки сохранены.");
    }
    gtk_widget_destroy(d);
    open = false;
}

// ------------------------------------------------------------------ подтверждение опасных действий (вызов из рабочего потока)
struct ConfirmReq { std::string what; bool res = false, done = false; std::mutex m; std::condition_variable cv; };
static gboolean ConfirmIdle(gpointer p) {
    ConfirmReq* r = (ConfirmReq*)p;
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(g_win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", r->what.c_str());
    bool yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
    gtk_widget_destroy(d);
    std::lock_guard<std::mutex> lk(r->m);
    r->res = yes;
    r->done = true;
    r->cv.notify_all();
    return G_SOURCE_REMOVE;
}
static bool ConfirmGui(const std::string& what) {
    ConfirmReq r;
    r.what = "FLEY AI хочет выполнить действие:\n\n" + what + "\n\nРазрешить?";
    g_idle_add(ConfirmIdle, &r);
    std::unique_lock<std::mutex> lk(r.m);
    r.cv.wait(lk, [&] { return r.done; });
    return r.res;
}

// ------------------------------------------------------------------ таймер интерфейса
static unsigned long long g_cpuTot = 0, g_cpuIdle = 0;
static void UpdateStats() {
    std::string s = ReadFileS("/proc/stat");
    unsigned long long u = 0, n = 0, sy = 0, id = 0, io = 0, irq = 0, sirq = 0, st = 0;
    if (sscanf(s.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &sy, &id, &io, &irq, &sirq, &st) >= 4) {
        unsigned long long tot = u + n + sy + id + io + irq + sirq + st, idl = id + io;
        if (g_cpuTot && tot > g_cpuTot) g_cpu = 100.0 * (1.0 - (double)(idl - g_cpuIdle) / (double)(tot - g_cpuTot));
        g_cpuTot = tot;
        g_cpuIdle = idl;
    }
    std::string mi = ReadFileS("/proc/meminfo");
    auto kb = [&](const char* k) { size_t p = mi.find(k); return p == std::string::npos ? 0.0 : atof(mi.c_str() + p + strlen(k)); };
    double tot = kb("MemTotal:"), av = kb("MemAvailable:");
    if (tot > 0) g_ram = 100.0 * (tot - av) / tot;
}
static gboolean OnTimer(gpointer) {
    static int tick = 0;
    g_t += 0.033;
    int st = g_state;
    double target = st == LISTENING ? g_level1000 / 1000.0 : 0.0;
    g_lv += (target - g_lv) * 0.25;
    if (++tick % 30 == 1) UpdateStats();
    if (g_showSet) { g_showSet = 0; OpenSettings(); }
    gtk_widget_queue_draw(g_area);
    return TRUE;
}

static void OnBtnMic(GtkButton*, gpointer) { OnOrbClick(); }
static void OnBtnSet(GtkButton*, gpointer) { OpenSettings(); }
static void OnBtnClear(GtkButton*, gpointer) {
    { std::lock_guard<std::mutex> lk(g_mx); g_log.clear(); }
    g_clearReq = 1;
}
static void OnBtnFull(GtkButton*, gpointer) { ToggleFull(); }

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    InitPaths();
    EnsureEnvFile();
    LoadSettings();
    LoadMemory();
    LoadHistory();
    gtk_init(&argc, &argv);

    g_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_win), "FLEY AI");
    gtk_window_set_default_size(GTK_WINDOW(g_win), 1100, 720);
    for (std::string p : {g_dir + "/fley.png", std::string("assets/fley.png")}) {
        std::error_code ec;
        if (fs::exists(p, ec)) { gtk_window_set_icon_from_file(GTK_WINDOW(g_win), p.c_str(), nullptr); break; }
    }
    g_area = gtk_drawing_area_new();
    gtk_widget_add_events(g_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(g_area, "draw", G_CALLBACK(OnDraw), nullptr);
    g_signal_connect(g_area, "button-press-event", G_CALLBACK(OnPress), nullptr);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(box, GTK_ALIGN_END);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(box, 54);
    gtk_widget_set_margin_end(box, 20);
    struct { const char* t; GCallback cb; } btns[] = {{"Микрофон", G_CALLBACK(OnBtnMic)}, {"Настройки", G_CALLBACK(OnBtnSet)},
                                                      {"Очистить", G_CALLBACK(OnBtnClear)}, {"Экран", G_CALLBACK(OnBtnFull)}};
    for (auto& b : btns) {
        GtkWidget* w = gtk_button_new_with_label(b.t);
        gtk_widget_set_can_focus(w, FALSE);
        g_signal_connect(w, "clicked", b.cb, nullptr);
        gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    }
    GtkWidget* ov = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(ov), g_area);
    gtk_overlay_add_overlay(GTK_OVERLAY(ov), box);
    gtk_container_add(GTK_CONTAINER(g_win), ov);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "button { background: rgba(10,30,50,0.75); color: #9fe9ff; border: 1px solid #1ec8ff; border-radius: 2px; padding: 4px 14px; font-weight: bold; background-image: none; }"
        "button:hover { background: rgba(30,200,255,0.28); }"
        "dialog, messagedialog { background: #07101c; } dialog label, messagedialog label { color: #bfefff; }"
        "entry { background: #0b1b2c; color: #e6fbff; border: 1px solid #1ec8ff; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_signal_connect(g_win, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) { QuitApp(); }), nullptr);
    g_signal_connect(g_win, "key-press-event", G_CALLBACK(OnKey), nullptr);
    g_confirmFn = ConfirmGui;

    if (g_set.window_mode == "mini") {
        gtk_window_resize(GTK_WINDOW(g_win), 420, 360);
        gtk_window_set_keep_above(GTK_WINDOW(g_win), TRUE);
    }
    gtk_widget_show_all(g_win);
    if (g_set.window_mode == "full") { g_full = true; gtk_window_fullscreen(GTK_WINDOW(g_win)); }

    std::thread(SessionThread).detach();
    g_timeout_add(33, OnTimer, nullptr);
    gtk_main();
    g_closing = 1;
    KillMicNow();
    _exit(0);  // рабочие потоки не ждём
}
