// FLEY AI - чистая логика терминала-агента (без системных вызовов): разбор блоков, правка файлов,
// проверка команд (scope, опасные команды), обрезка вывода. Тестируется отдельно (tests/test_agent.cpp).
#pragma once
#include "core.hpp"

#include <cstdint>
#include <regex>
#include <set>

namespace fley {

// ---------------------------------------------------------------- вывод
inline std::string strip_ansi(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1B) {  // ESC
            if (i + 1 < s.size() && s[i + 1] == '[') {  // CSI ... буква
                i += 2;
                while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
            } else if (i + 1 < s.size() && s[i + 1] == ']') {  // OSC ... BEL или ESC \ .
                i += 2;
                while (i < s.size() && s[i] != 7 && !(s[i] == 0x1B && i + 1 < s.size() && s[i + 1] == '\\')) ++i;
                if (i < s.size() && s[i] == 0x1B) ++i;
            } else {
                ++i;
            }
            continue;
        }
        if (c == '\r') {  // возврат каретки (прогресс-бары): оставляем только последний вариант строки
            if (i + 1 < s.size() && s[i + 1] == '\n') continue;
            size_t ls = o.rfind('\n');
            o.erase(ls == std::string::npos ? 0 : ls + 1);
            continue;
        }
        if (c == 0 || (c < 9) || (c > 13 && c < 32 && c != 27)) continue;
        o += (char)c;
    }
    return o;
}

// Если вывод длиннее лимита: начало (2/3) + конец (1/3). В конце обычно итог и ошибка.
inline std::string head_tail(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t h = max_bytes * 2 / 3, t = max_bytes - h;
    std::string head = utf8_truncate(s, h);
    size_t st = s.size() - t;
    while (st < s.size() && ((unsigned char)s[st] & 0xC0) == 0x80) ++st;
    return head + "\n…[пропущено " + std::to_string(s.size() - head.size() - (s.size() - st)) + " байт]…\n" + s.substr(st);
}

// ---------------------------------------------------------------- правка файла по фрагменту
// Заменяет ровно одно вхождение old_s на new_s. Возвращает false и текст ошибки, если фрагмент не найден или не уникален.
inline bool apply_edit(std::string& content, const std::string& old_s, const std::string& new_s, std::string& err) {
    if (old_s.empty()) { err = "Пустой фрагмент [[FIND]]."; return false; }
    size_t p = content.find(old_s);
    if (p == std::string::npos) {
        // частая беда: разный перевод строк или пробелы на концах строк
        err = "Фрагмент [[FIND]] не найден в файле (проверьте точность: пробелы, отступы, регистр). Прочитайте файл read_file и повторите.";
        return false;
    }
    if (content.find(old_s, p + 1) != std::string::npos) {
        size_t n = 0, q = 0;
        while ((q = content.find(old_s, q)) != std::string::npos) { ++n; q += 1; }
        err = "Фрагмент [[FIND]] встречается " + std::to_string(n) + " раз(а): добавьте соседние строки, чтобы он был уникальным.";
        return false;
    }
    content.replace(p, old_s.size(), new_s);
    return true;
}

// ---------------------------------------------------------------- разбор блоков [[SH]] [[WRITE: путь]] [[EDIT: путь]] ... [[END]]
inline std::string strip_one_nl(std::string s) {
    if (!s.empty() && s.front() == '\n') s.erase(0, 1);
    else if (s.size() >= 2 && s[0] == '\r' && s[1] == '\n') s.erase(0, 2);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}
// Модель иногда оборачивает содержимое в ```...```: снимаем обёртку
inline std::string strip_code_fence(const std::string& s) {
    std::string t = s;
    size_t a = 0;
    while (a < t.size() && (t[a] == ' ' || t[a] == '\t')) ++a;
    if (t.compare(a, 3, "```") != 0) return s;
    size_t nl = t.find('\n', a);
    if (nl == std::string::npos) return s;
    size_t e = t.size();
    while (e > nl && (t[e - 1] == '\n' || t[e - 1] == ' ' || t[e - 1] == '\r' || t[e - 1] == '\t')) --e;
    if (e < nl + 4 || t.compare(e - 3, 3, "```") != 0) return s;
    std::string body = t.substr(nl + 1, e - 3 - (nl + 1));
    if (!body.empty() && body.back() == '\n') body.pop_back();
    return body;
}

inline bool parse_edit_pairs(const std::string& content, std::vector<std::string>& pairs) {
    size_t pos = 0;
    bool any = false;
    while (true) {
        size_t f = content.find("[[FIND]]", pos);
        if (f == std::string::npos) break;
        size_t w = content.find("[[WITH]]", f + 8);
        if (w == std::string::npos) return false;
        size_t nf = content.find("[[FIND]]", w + 8);
        std::string o = strip_one_nl(content.substr(f + 8, w - f - 8));
        std::string n = strip_one_nl(content.substr(w + 8, (nf == std::string::npos ? content.size() : nf) - w - 8));
        pairs.push_back(o);
        pairs.push_back(n);
        any = true;
        pos = nf == std::string::npos ? content.size() : nf;
    }
    return any;
}

// Весь разбор ответа модели. Теги выполняются по порядку появления.
//  [[ACTION: тип | арг | арг]]           обычные действия (для run и sh вся строка после первой | - это команда)
//  [[SH]]\nкоманда\n[[END]]             команда bash любой длины и сложности; [[SH: 300]] - с таймаутом в секундах
//  [[WRITE: путь]]\nтекст\n[[END]]      записать файл целиком ([[APPEND: путь]] - дописать)
//  [[EDIT: путь]]\n[[FIND]]\nстарое\n[[WITH]]\nновое\n[[END]]   заменить фрагмент
inline std::string extract_actions_ex(const std::string& in, std::vector<Action>& out) {
    std::string text;
    size_t pos = 0;
    while (true) {
        size_t a = in.find("[[", pos);
        if (a == std::string::npos) { text += in.substr(pos); break; }
        size_t b = in.find("]]", a + 2);
        if (b == std::string::npos) { text += in.substr(pos); break; }
        std::string inner = trim(in.substr(a + 2, b - a - 2));
        std::string low = to_lower_ascii(inner);
        size_t colon = low.find(':');
        std::string name = trim(colon == std::string::npos ? low : low.substr(0, colon));
        std::string head = colon == std::string::npos ? "" : trim(inner.substr(colon + 1));
        bool block = inner.size() < 1500 && inner.find('\n') == std::string::npos &&
                     (name == "sh" || name == "write" || name == "append" || name == "edit");
        if (low.compare(0, 7, "action:") == 0) {
            text += in.substr(pos, a - pos);
            std::string body = inner.substr(7);
            Action act;
            size_t st = 0;
            bool first = true;
            while (true) {
                size_t bar = body.find('|', st);
                std::string part = trim(body.substr(st, bar == std::string::npos ? std::string::npos : bar - st));
                if (first) { act.type = to_lower_ascii(part); first = false; }
                else act.args.push_back(part);
                if (bar == std::string::npos) break;
                st = bar + 1;
            }
            if ((act.type == "run" || act.type == "sh") && !act.args.empty()) {  // в команде могут быть | - берём всё как есть
                size_t bar = body.find('|');
                act.args.assign(1, trim(body.substr(bar + 1)));
            }
            if (!act.type.empty()) out.push_back(act);
            pos = b + 2;
        } else if (block) {
            text += in.substr(pos, a - pos);
            size_t e = in.find("[[END]]", b + 2);
            if (e == std::string::npos) {
                Action er;
                er.type = "error";
                er.args.push_back("Блок [[" + name + "]] не закрыт тегом [[END]]: ответ был обрезан, ничего не выполнено. Повторите короче или разбейте на части.");
                out.push_back(er);
                pos = in.size();
                break;
            }
            std::string content = strip_one_nl(in.substr(b + 2, e - b - 2));
            Action act;
            if (name == "sh") {
                act.type = "sh";
                act.args.push_back(strip_code_fence(content));
                if (!head.empty()) act.args.push_back(head);
            } else if (name == "write" || name == "append") {
                act.type = name == "write" ? "write_file" : "append_file";
                act.args.push_back(head);
                act.args.push_back(strip_code_fence(content));
            } else {
                act.type = "edit_file";
                act.args.push_back(head);
                std::vector<std::string> pairs;
                if (!parse_edit_pairs(content, pairs)) {
                    act.type = "error";
                    act.args.assign(1, "Блок [[EDIT]] без пары [[FIND]] ... [[WITH]] ...: файл не изменён.");
                } else {
                    for (auto& p : pairs) act.args.push_back(p);
                }
            }
            out.push_back(act);
            pos = e + 7;
        } else {
            text += in.substr(pos, b + 2 - pos);
            pos = b + 2;
        }
    }
    return trim(text);
}

// Для истории разговора: тела больших блоков записи файла заменяем пометкой, чтобы они не раздували каждый следующий запрос
inline std::string compact_blocks(const std::string& in) {
    std::string o;
    size_t pos = 0;
    while (true) {
        size_t a = in.find("[[", pos);
        if (a == std::string::npos) { o += in.substr(pos); break; }
        size_t b = in.find("]]", a + 2);
        if (b == std::string::npos) { o += in.substr(pos); break; }
        std::string low = to_lower_ascii(trim(in.substr(a + 2, b - a - 2)));
        bool fileBlock = low.compare(0, 6, "write:") == 0 || low.compare(0, 7, "append:") == 0 || low.compare(0, 5, "edit:") == 0;
        size_t e = fileBlock ? in.find("[[END]]", b + 2) : std::string::npos;
        if (fileBlock && e != std::string::npos && e - b > 500) {
            o += in.substr(pos, b + 2 - pos) + "\n…[" + std::to_string(e - b - 2) + " байт содержимого, выполнено]…\n[[END]]";
            pos = e + 7;
        } else {
            o += in.substr(pos, b + 2 - pos);
            pos = b + 2;
        }
    }
    return o;
}

// ---------------------------------------------------------------- слова команды
inline std::string path_base(const std::string& p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
// грубое деление команды на слова: разделители оболочки превращаются в пробелы, кавычки снимаются
inline std::vector<std::string> cmd_tokens(const std::string& cmd) {
    std::string t;
    t.reserve(cmd.size());
    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
        if (c == ';' || c == '|' || c == '&' || c == '(' || c == ')' || c == '`' || c == '\n' || c == '{' || c == '}' || c == '<' || c == '>') t += ' ';
        else if (c == '$' && i + 1 < cmd.size() && cmd[i + 1] == '(') t += ' ';
        else if (c == '"' || c == '\'') t += ' ';  // "a b" разобьётся на слова, но для поиска целей этого достаточно
        else t += c;
    }
    std::vector<std::string> w;
    std::string cur;
    for (char c : t) {
        if (c == ' ' || c == '\t' || c == '\r') { if (!cur.empty()) { w.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

// ---------------------------------------------------------------- scope (разрешённые цели)
inline bool ipv4_parse(const std::string& s, uint32_t& out) {
    unsigned a, b, c, d;
    char x;
    if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &x) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    for (char ch : s) if (!(isdigit((unsigned char)ch) || ch == '.')) return false;
    out = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}
struct Cidr { uint32_t net = 0, mask = 0; int bits = 32; };
inline bool cidr_parse(const std::string& s, Cidr& c) {
    size_t sl = s.find('/');
    std::string ip = sl == std::string::npos ? s : s.substr(0, sl);
    uint32_t v;
    if (!ipv4_parse(ip, v)) return false;
    int bits = 32;
    if (sl != std::string::npos) {
        std::string b = s.substr(sl + 1);
        if (b.empty() || b.size() > 2) return false;
        for (char ch : b) if (!isdigit((unsigned char)ch)) return false;
        bits = atoi(b.c_str());
        if (bits < 0 || bits > 32) return false;
    }
    c.bits = bits;
    c.mask = bits == 0 ? 0 : (0xFFFFFFFFu << (32 - bits));
    c.net = v & c.mask;
    return true;
}
inline bool is_loopback_host(const std::string& h) {
    std::string l = to_lower_ascii(h);
    if (l == "localhost" || l == "::1" || l == "[::1]" || l == "0.0.0.0" || l == "ip6-localhost") return true;
    uint32_t v;
    if (ipv4_parse(l, v) && (v >> 24) == 127) return true;
    return false;
}
inline std::vector<std::string> parse_scope(const std::string& text) {
    std::vector<std::string> o;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        size_t h = line.find('#');
        if (h != std::string::npos) line.erase(h);
        line = to_lower_ascii(trim(line));
        if (!line.empty()) o.push_back(line);
    }
    return o;
}
inline bool domain_in_scope(const std::string& host, const std::vector<std::string>& scope) {
    std::string h = to_lower_ascii(host);
    while (!h.empty() && h.back() == '.') h.pop_back();
    for (std::string s : scope) {
        if (s.compare(0, 2, "*.") == 0) s.erase(0, 2);
        if (s.empty()) continue;
        if (h == s) return true;
        if (h.size() > s.size() + 1 && h.compare(h.size() - s.size() - 1, s.size() + 1, "." + s) == 0) return true;
    }
    return false;
}
// target: IPv4, CIDR, диапазон a.b.c.1-50, a.b.c.* или имя хоста/домен
inline bool target_in_scope(const std::string& target, const std::vector<std::string>& scope) {
    std::string t = to_lower_ascii(target);
    if (is_loopback_host(t)) return true;
    // a.b.c.*  и  a.b.c.x-y
    std::vector<std::pair<uint32_t, uint32_t>> ranges;  // [lo, hi]
    Cidr tc;
    size_t dash = t.rfind('-');
    if (t.size() > 2 && t.compare(t.size() - 2, 2, ".*") == 0) {
        uint32_t base;
        if (ipv4_parse(t.substr(0, t.size() - 2) + ".0", base)) ranges.push_back({base, base + 255});
    } else if (dash != std::string::npos && t.find('/') == std::string::npos && t.find('.') != std::string::npos) {
        std::string left = t.substr(0, dash), right = t.substr(dash + 1);
        uint32_t lo;
        size_t ld = left.rfind('.');
        bool numRight = !right.empty() && right.size() <= 3;
        for (char ch : right) if (!isdigit((unsigned char)ch)) numRight = false;
        if (ld != std::string::npos && numRight && ipv4_parse(left, lo)) {
            unsigned hiOct = (unsigned)atoi(right.c_str());
            if (hiOct > 255) return false;
            ranges.push_back({lo, (lo & 0xFFFFFF00u) | hiOct});
        }
    }
    if (ranges.empty() && cidr_parse(t, tc)) ranges.push_back({tc.net, tc.net | ~tc.mask});
    if (!ranges.empty()) {
        for (auto& r : ranges) {
            if ((r.first >> 24) == 127 && (r.second >> 24) == 127) continue;
            bool ok = false;
            for (const std::string& s : scope) {
                Cidr sc;
                if (cidr_parse(s, sc) && (r.first & sc.mask) == sc.net && (r.second & sc.mask) == sc.net) { ok = true; break; }
                size_t sd = s.rfind('-');  // запись диапазона в scope: 10.0.0.1-50
                if (sd != std::string::npos && s.find('/') == std::string::npos) {
                    uint32_t lo;
                    std::string l = s.substr(0, sd), rr = s.substr(sd + 1);
                    if (ipv4_parse(l, lo) && !rr.empty() && rr.size() <= 3) {
                        uint32_t hi = (lo & 0xFFFFFF00u) | (unsigned)atoi(rr.c_str());
                        if (r.first >= lo && r.second <= hi) { ok = true; break; }
                    }
                }
            }
            if (!ok) return false;
        }
        return true;
    }
    for (const std::string& s : scope) if (s == t) return true;  // точное совпадение (в т.ч. IPv6)
    return domain_in_scope(t, scope);
}

// Извлекает из слова команды цель сети (хост/IP/CIDR). Возвращает false, если слово целью не выглядит.
inline bool extract_target(std::string tok, std::string& host, bool& unresolved) {
    unresolved = false;
    if (tok.empty()) return false;
    if (tok[0] == '-') {  // --url=http://x, --target=10.0.0.1
        size_t eq = tok.find('=');
        if (tok.compare(0, 2, "--") != 0 || eq == std::string::npos) return false;
        tok = tok.substr(eq + 1);
        if (tok.empty()) return false;
    }
    if (tok[0] == '$') { unresolved = true; return false; }
    if (tok[0] == '/' || tok[0] == '.' || tok[0] == '~') return false;
    bool scheme = false;
    size_t sp = tok.find("://");
    if (sp != std::string::npos) {
        std::string sc = to_lower_ascii(tok.substr(0, sp));
        if (sc.empty() || sc.size() > 10) return false;
        tok = tok.substr(sp + 3);
        scheme = true;
    }
    size_t cut = tok.find_first_of("/?#");
    std::string auth = cut == std::string::npos ? tok : tok.substr(0, cut);
    size_t at = auth.rfind('@');
    if (at != std::string::npos) { auth = auth.substr(at + 1); scheme = true; }
    if (auth.empty()) return false;
    std::string cidrTail;
    if (cut != std::string::npos && tok[cut] == '/' && !scheme) {  // 10.0.0.0/24
        std::string rest = tok.substr(cut + 1);
        bool digits = !rest.empty() && rest.size() <= 2;
        for (char ch : rest) if (!isdigit((unsigned char)ch)) digits = false;
        uint32_t v;
        if (digits && ipv4_parse(auth, v)) cidrTail = "/" + rest;
        else if (!rest.empty()) return false;  // относительный путь вида dir/file
    }
    // порт
    if (auth.front() == '[') {
        size_t rb = auth.find(']');
        if (rb == std::string::npos) return false;
        host = auth.substr(0, rb + 1);
        return true;
    }
    size_t colons = std::count(auth.begin(), auth.end(), ':');
    if (colons >= 2) {  // IPv6
        for (char ch : auth) if (!(isxdigit((unsigned char)ch) || ch == ':' || ch == '.')) return false;
        host = auth;
        return true;
    }
    if (colons == 1) {
        size_t c = auth.find(':');
        std::string port = auth.substr(c + 1);
        for (char ch : port) if (!isdigit((unsigned char)ch)) return false;
        auth = auth.substr(0, c);
    }
    std::string l = to_lower_ascii(auth);
    // IPv4, CIDR, a.b.c.*, диапазон
    {
        Cidr c;
        std::string probe = l + cidrTail;
        if (cidr_parse(probe, c)) { host = probe; return true; }
        if (l.size() > 2 && l.compare(l.size() - 2, 2, ".*") == 0) {
            uint32_t v;
            if (ipv4_parse(l.substr(0, l.size() - 2) + ".0", v)) { host = l; return true; }
        }
        size_t dash = l.rfind('-');
        if (dash != std::string::npos) {
            uint32_t v;
            std::string left = l.substr(0, dash), right = l.substr(dash + 1);
            bool numRight = !right.empty() && right.size() <= 3;
            for (char ch : right) if (!isdigit((unsigned char)ch)) numRight = false;
            if (numRight && ipv4_parse(left, v)) { host = l; return true; }
        }
    }
    if (l == "localhost") { host = l; return true; }
    // имя хоста: a.b.tld
    static const std::set<std::string> fileExt = {"txt", "log", "xml", "json", "md", "py", "sh", "conf", "cfg", "html", "htm", "php", "js", "css", "csv", "lst",
                                                   "gnmap", "nmap", "out", "pcap", "pcapng", "bak", "zip", "gz", "tar", "c", "cpp", "h", "hpp", "so", "o", "rb",
                                                   "pl", "yml", "yaml", "ini", "db", "sql", "bin", "exe", "dll", "jpg", "png", "gif", "pdf", "doc", "docx", "xls",
                                                   "xlsx", "rs", "go", "java", "class", "jar", "toml", "lock", "pem", "key", "crt", "cer", "pub", "hash", "dic",
                                                   "wordlist", "list", "tmp", "swp", "orig", "diff", "patch", "ps1", "bat", "cmd", "svg", "ico", "txt2", "xsl"};
    size_t dot = l.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= l.size()) return false;
    std::string tld = l.substr(dot + 1);
    for (char ch : l) if (!(isalnum((unsigned char)ch) || ch == '.' || ch == '-')) return false;
    if (tld.size() < 2 || tld.size() > 24) return false;
    for (char ch : tld) if (!isalpha((unsigned char)ch)) return false;
    if (!scheme && fileExt.count(tld)) return false;
    host = l;
    return true;
}

// ---------------------------------------------------------------- классы инструментов
inline bool is_net_tool(const std::string& w) {
    return one_of(w, {"nmap", "masscan", "rustscan", "zmap", "nikto", "gobuster", "feroxbuster", "dirb", "dirsearch", "ffuf", "wfuzz", "whatweb", "wpscan",
                      "nuclei", "sqlmap", "enum4linux", "enum4linux-ng", "smbmap", "hydra", "medusa", "ncrack", "patator", "netexec", "nxc", "crackmapexec",
                      "testssl", "testssl.sh", "sslscan", "sslyze", "amass", "subfinder", "dnsrecon", "fierce", "theharvester", "wapiti", "joomscan",
                      "arp-scan", "netdiscover", "hping3", "nbtscan", "snmpwalk", "onesixtyone", "ike-scan", "msfconsole", "commix", "dalfox", "httpx",
                      "naabu", "katana", "gospider", "arjun", "nc", "ncat", "netcat", "socat", "dnsenum", "wafw00f", "sslstrip", "responder", "bettercap",
                      "ettercap", "evil-winrm", "impacket-smbclient", "smbclient", "rpcclient", "ldapsearch", "showmount", "xsstrike"});
}
inline bool is_intrusive_tool(const std::string& w) {
    return one_of(w, {"hydra", "medusa", "ncrack", "patator", "sqlmap", "msfconsole", "commix", "hping3", "aireplay-ng", "mdk4", "mdk3", "ettercap", "bettercap",
                      "responder", "netexec", "nxc", "crackmapexec", "masscan", "zmap", "evil-winrm", "sslstrip", "dalfox", "xsstrike"});
}
inline bool touches_network(const std::vector<std::string>& w) {
    for (auto& t : w) {
        std::string b = to_lower_ascii(path_base(t));
        if (is_net_tool(b) || one_of(b, {"curl", "wget", "ssh", "scp", "sftp", "telnet", "ftp", "dig", "whois", "host", "nslookup", "ping", "traceroute",
                                          "tracepath", "mtr", "aria2c", "lynx", "w3m", "links"})) return true;
    }
    return false;
}

// Команды, которые только читают и ничего не меняют
inline bool is_readonly_safe(const std::string& cmd) {
    if (cmd.find('>') != std::string::npos || cmd.find('`') != std::string::npos || cmd.find("$(") != std::string::npos) return false;
    if (cmd.find("-exec") != std::string::npos || cmd.find("-delete") != std::string::npos || cmd.find("-ok") != std::string::npos) return false;
    size_t pos = 0;
    bool any = false;
    while (pos <= cmd.size()) {
        size_t e = cmd.find_first_of(";|&\n", pos);
        if (e == std::string::npos) e = cmd.size();
        std::string seg = trim(cmd.substr(pos, e - pos));
        pos = e + 1;
        if (seg.empty()) continue;
        std::vector<std::string> w = cmd_tokens(seg);
        if (w.empty()) continue;
        std::string f = path_base(w[0]);
        if (!one_of(f, {"ls", "cat", "head", "tail", "grep", "egrep", "fgrep", "rg", "wc", "file", "stat", "pwd", "echo", "printf", "id", "whoami", "uname", "date",
                        "df", "du", "ps", "ip", "ss", "netstat", "sort", "uniq", "cut", "tr", "strings", "xxd", "hexdump", "od", "sha256sum", "sha1sum", "md5sum",
                        "base64", "which", "type", "command", "env", "tree", "less", "more", "basename", "dirname", "realpath", "readlink", "nproc", "free", "uptime",
                        "lsblk", "lscpu", "lsusb", "lspci", "hostname", "whereis", "find", "diff", "cmp", "jq", "column", "nl", "tac", "rev", "seq", "true", "false",
                        "cd", "test", "["}))
            return false;
        any = true;
    }
    return any;
}

// ---------------------------------------------------------------- проверка команды перед запуском
struct Gate {
    int level = 0;  // 0 - можно, 1 - спросить пользователя в окне, 2 - заблокировать
    std::string reason;
};
inline void gate_raise(Gate& g, int level, const std::string& reason) {
    if (level > g.level) { g.level = level; g.reason = reason; }
    else if (level == g.level && !reason.empty() && g.reason.find(reason) == std::string::npos) g.reason += "; " + reason;
}

inline bool path_under(const std::string& p, const std::string& root) {
    if (root.empty()) return false;
    if (p.find("..") != std::string::npos) return false;
    return p == root || p.compare(0, root.size() + 1, root + "/") == 0;
}

// cmd - текст команды; scope - разрешённые цели; secmode - режим исследователя ИБ; safe_roots - где можно удалять без вопросов
inline Gate gate_command(const std::string& cmd, const std::vector<std::string>& scope, bool secmode, const std::vector<std::string>& safe_roots, bool tainted,
                         bool confirm_all, const std::string& cwd = "") {
    Gate g;
    std::vector<std::string> w = cmd_tokens(cmd);
    std::vector<std::string> base;
    for (auto& t : w) base.push_back(to_lower_ascii(path_base(t)));

    bool anyNet = false, anyIntrusive = false;
    for (auto& b : base) { if (is_net_tool(b)) anyNet = true; if (is_intrusive_tool(b)) anyIntrusive = true; }

    // 1. scope: цели сканеров и атакующих инструментов только из scope.txt
    bool scopedOk = false;
    if (secmode && anyNet) {
        std::string bad;
        bool unresolved = false, listFile = false;
        for (size_t i = 0; i < w.size(); ++i) {
            if (w[i] == "-iL" || w[i] == "-iR" || w[i] == "--input-list") listFile = true;
            std::string host;
            bool un = false;
            if (is_net_tool(base[i]) && i == 0) continue;
            if (extract_target(w[i], host, un)) {
                if (!target_in_scope(host, scope)) bad += (bad.empty() ? "" : ", ") + host;
            }
            if (un) unresolved = true;
        }
        if (!bad.empty())
            gate_raise(g, 2, "цель вне scope: " + bad + ". Если это ваша система или есть письменное разрешение, добавьте цель в scope (действие scope_add, пользователь подтвердит окном).");
        else if (listFile)
            gate_raise(g, 1, "цели берутся из файла: проверить их по scope нельзя");
        else if (unresolved)
            gate_raise(g, 1, "в команде переменная вместо цели: проверить её по scope нельзя");
        else
            scopedOk = true;
    }
    if (g.level == 2) return g;

    // 2. опасные команды: всегда спрашиваем
    auto has = [&](std::initializer_list<const char*> l) { for (auto& b : base) if (one_of(b, l)) return true; return false; };
    if (has({"sudo", "su", "doas", "pkexec"})) gate_raise(g, 1, "повышение прав (sudo)");
    if (has({"mkfs", "mkfs.ext4", "mkfs.ext3", "mkfs.xfs", "mkfs.btrfs", "mkfs.vfat", "mkfs.ntfs", "wipefs", "shred", "fdisk", "sfdisk", "parted", "mkswap", "cryptsetup"}) ||
        cmd.find("mkfs.") != std::string::npos)
        gate_raise(g, 1, "работа с дисками или форматирование");
    if (has({"dd"}) && cmd.find("of=") != std::string::npos) gate_raise(g, 1, "dd с записью");
    if (has({"shutdown", "reboot", "poweroff", "halt", "init"})) gate_raise(g, 1, "выключение или перезагрузка");
    if (has({"passwd", "usermod", "userdel", "useradd", "groupadd", "groupdel", "visudo", "chpasswd", "adduser", "deluser"})) gate_raise(g, 1, "изменение учётных записей");
    if (has({"iptables", "ip6tables", "nft", "ufw", "firewall-cmd"})) gate_raise(g, 1, "изменение файрвола");
    if (cmd.find(":(){") != std::string::npos || cmd.find(":() {") != std::string::npos) gate_raise(g, 1, "похоже на fork-бомбу");
    if (cmd.find("/dev/tcp/") != std::string::npos || cmd.find("/dev/udp/") != std::string::npos) gate_raise(g, 1, "сетевое соединение через /dev/tcp (reverse shell?)");
    for (size_t i = 0; i < w.size(); ++i) {
        const std::string& b = base[i];
        auto arg = [&](size_t k) { return k < w.size() ? w[k] : std::string(); };
        if (b == "rm" || b == "rmdir" || b == "unlink") {
            bool rec = false;
            std::vector<std::string> ops;
            for (size_t k = i + 1; k < w.size(); ++k) {
                if (w[k].size() > 1 && w[k][0] == '-') { if (w[k].compare(0, 2, "--") == 0 ? (w[k] == "--recursive") : (w[k].find('r') != std::string::npos || w[k].find('R') != std::string::npos)) rec = true; }
                else ops.push_back(w[k]);
            }
            bool allSafe = !ops.empty();
            for (auto& o : ops) {
                bool s = false;
                std::string ab = (!o.empty() && (o[0] == '/' || o[0] == '~' || o[0] == '$')) ? o : (cwd.empty() ? o : cwd + "/" + o);
                for (auto& r : safe_roots) if (path_under(ab, r)) s = true;
                if (!s) allSafe = false;
            }
            if (rec && !allSafe) gate_raise(g, 1, "рекурсивное удаление вне рабочей папки");
            else if (!allSafe && ops.empty()) gate_raise(g, 1, "удаление");
            else if (!rec && !allSafe) {
                bool wild = false;
                for (auto& o : ops) if (o.find('*') != std::string::npos || o == "/" || o == "~" || o == "$HOME") wild = true;
                if (wild) gate_raise(g, 1, "удаление по маске вне рабочей папки");
            }
        }
        if ((b == "chmod" || b == "chown" || b == "chgrp") && (arg(i + 1) == "-R" || arg(i + 1) == "-r" || arg(i + 1) == "--recursive"))
            gate_raise(g, 1, "рекурсивная смена прав");
        if ((b == "apt" || b == "apt-get" || b == "dnf" || b == "yum" || b == "zypper" || b == "pacman")) {
            for (size_t k = i + 1; k < w.size(); ++k)
                if (one_of(w[k], {"remove", "purge", "autoremove", "erase", "-R", "-Rs", "-Rns", "-Rdd", "rm"})) { gate_raise(g, 1, "удаление пакетов"); break; }
        }
        if (b == "systemctl")
            for (size_t k = i + 1; k < w.size(); ++k)
                if (one_of(w[k], {"stop", "disable", "mask", "kill", "poweroff", "reboot", "halt"})) { gate_raise(g, 1, "остановка служб"); break; }
        if (b == "git") {
            for (size_t k = i + 1; k < w.size(); ++k) {
                if (w[k] == "push" && (cmd.find("--force") != std::string::npos || cmd.find(" -f") != std::string::npos)) { gate_raise(g, 1, "git push --force"); break; }
                if (w[k] == "reset" && cmd.find("--hard") != std::string::npos) { gate_raise(g, 1, "git reset --hard"); break; }
                if (w[k] == "clean" && cmd.find("-f") != std::string::npos) { gate_raise(g, 1, "git clean -f"); break; }
            }
        }
        if (b == "kill" || b == "killall" || b == "pkill") {
            for (size_t k = i + 1; k + 1 < w.size(); ++k)
                if ((w[k] == "-9" || w[k] == "-KILL" || w[k] == "-TERM") && w[k + 1] == "-1") gate_raise(g, 1, "kill всех процессов");
        }
        // отправка данных наружу
        if (b == "curl") {
            for (size_t k = i + 1; k < w.size(); ++k) {
                const std::string& a = w[k];
                bool upload = a == "-T" || a == "--upload-file" || a == "-F" || a == "--form" ||
                              ((a == "-d" || a == "--data" || a == "--data-binary" || a == "--data-raw" || a == "--data-urlencode") && k + 1 < w.size() && w[k + 1][0] == '@') ||
                              a.compare(0, 8, "--data=@") == 0 || a.compare(0, 14, "--data-binary=") == 0;
                if (upload) { gate_raise(g, 1, "отправка файла или данных на сервер"); break; }
            }
        }
        if (b == "scp" || b == "rsync" || b == "sftp") gate_raise(g, 1, "копирование на другой компьютер");
        if ((b == "nc" || b == "ncat" || b == "netcat") && (cmd.find(" -e") != std::string::npos || cmd.find("--exec") != std::string::npos || cmd.find("--sh-exec") != std::string::npos))
            gate_raise(g, 1, "netcat с запуском оболочки");
    }
    // curl ... | sh
    {
        static const std::regex pipeShell("\\|\\s*(sudo\\s+)?(env\\s+)?(ba|z|da|k)?sh(\\s|$|-)", std::regex::ECMAScript);
        if (std::regex_search(cmd, pipeShell)) gate_raise(g, 1, "запуск скачанного кода через конвейер в sh");
        if (cmd.find("base64 -d") != std::string::npos && (cmd.find("| sh") != std::string::npos || cmd.find("|sh") != std::string::npos || cmd.find("bash") != std::string::npos))
            gate_raise(g, 1, "запуск закодированного кода");
    }
    // запись в системные места и конфиги пользователя
    {
        static const std::regex sysWrite("(>>?|tee(\\s+-a)?)\\s*(/etc/|/boot/|/dev/(sd|nvme|vd|hd|mmcblk)|/usr/|/bin/|/sbin/|/lib|/var/spool/cron|~/\\.ssh|\\$HOME/\\.ssh|~/\\.bashrc|~/\\.profile|~/\\.zshrc)",
                                         std::regex::ECMAScript);
        if (std::regex_search(cmd, sysWrite)) gate_raise(g, 1, "запись в системные файлы или настройки входа");
        if (cmd.find("authorized_keys") != std::string::npos || cmd.find("crontab") != std::string::npos) gate_raise(g, 1, "ключи входа или планировщик");
        if (cmd.find("fley-ai/.env") != std::string::npos || cmd.find("scope.txt") != std::string::npos) gate_raise(g, 1, "настройки или scope FLEY AI");
    }

    // 3. атакующие инструменты: всегда спрашиваем (даже внутри scope)
    if (secmode && anyIntrusive) {
        bool onlyLoop = true;  // против loopback можно без вопросов
        for (auto& t : w) { std::string host; bool un; if (extract_target(t, host, un) && !is_loopback_host(host)) onlyLoop = false; }
        if (!onlyLoop || w.size() <= 1) gate_raise(g, 1, "активный/атакующий инструмент: подтвердите запуск");
    }

    // 4. пользователь потребовал спрашивать всё
    if (confirm_all) gate_raise(g, 1, "включено подтверждение всех команд");

    // 5. в этом запросе уже читали недоверенные данные из сети: меняющие команды - только с подтверждением
    if (tainted && g.level == 0 && !is_readonly_safe(cmd) && !scopedOk)
        gate_raise(g, 1, "в этом запросе уже были получены недоверенные данные из сети или файлов");
    return g;
}

}  // namespace fley
