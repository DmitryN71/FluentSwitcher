// Списки автопереключения, которые можно пополнять без выпуска программы (HANDOFF.md, раздел 12): сокращения,
// сокращения с точками, зоны сайтов и расширения файлов, слова перед числом, "не слова" и нейтральные слова. Здесь -
// встроенные; файл правил (rules.json) их дополняет и убирает из них слова, списки пользователя главнее всего.
// Правила - как решается, переключать ли слово, - в AutoSwitch.h, они меняются только с программой.
// Current() читает поток перехвата на каждом слове, Set() подменяет списки целиком из другого потока: прежние не
// удаляются (их могут читать прямо сейчас; подмена - раз в день, списки маленькие).
// Без wxWidgets: нужен и движку, и окну настроек.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace Rules {

// ----- Сокращения, которые словарь не решит (AutoSwitch::KnownShort) -----
// Аббревиатуры, слова без гласных, названия клавиш, единицы (Дмитрий, 15.10.2026: "ГЫИ" - USB не переключалось,
// "понимаю, что Caramba пока впереди"): прописные короче четырёх букв и слова без гласных Skip не трогает, а словарь
// Windows прописные принимает любые. Свой список на язык: там - сокращение из списка (kShortEn, kShortRu), набранное - не
// слово своего языка (словарь, и строчными) и не из своих списков. ...Keep - что набирают нарочно и что не
// переводить ("ru" - не "кг", "cv" - не "см", "CIF" - не "США", "DC" - не "вс"). Без цифр: "А4" - формат листа, не F4.
inline constexpr const wchar_t* kShortEn[] = {
	L"usb", L"pdf", L"api", L"crm", L"sku", L"vpn", L"warp", L"mcp", L"sqt", L"smb", L"dns", L"mit", L"ui", L"ux", L"url", L"uri", L"html", L"css", L"json",
	L"xml", L"yaml", L"sql", L"http", L"https", L"ftp", L"sftp", L"ssh", L"ssl", L"tls", L"tcp", L"udp", L"ip", L"vps",
	L"cpu", L"gpu", L"ram", L"ssd", L"hdd", L"nvme", L"bios", L"uefi", L"os", L"ios", L"lan", L"wan", L"wifi", L"nfc",
	L"gps", L"seo", L"smm", L"cms", L"sdk", L"ide", L"gui", L"cli", L"pc", L"tv", L"hd", L"led", L"lcd", L"oled", L"rgb",
	L"hdmi", L"dvd", L"faq", L"ceo", L"cfo", L"cto", L"hr", L"pr", L"qa", L"mvp", L"saas", L"ai", L"ml", L"gpt", L"llm",
	L"ocr", L"docx", L"xlsx", L"pptx", L"csv", L"txt", L"jpg", L"jpeg", L"png", L"gif", L"svg", L"wav", L"zip", L"rar",
	L"exe", L"dll", L"msi", L"iso", L"pls", L"plz", L"thx", L"thnx", L"btw", L"tbh", L"idk", L"imo", L"imho", L"lmk",
	L"brb", L"omg", L"msg", L"npm", L"src", L"cmd", L"ls", L"pwd", L"sudo", L"env", L"js", L"ts", L"py", L"cpp", L"php",
	L"db", L"ctrl", L"alt", L"esc", L"del", L"ins", L"pgup", L"pgdn", L"prtsc", L"fn", L"ghz", L"mhz", L"khz", L"kb",
	L"mb", L"gb", L"tb", L"px", L"dpi", L"fps", L"rpm", L"wb", L"ym", L"ozon", L"fbo", L"fbs", L"dbs", L"rfbs", L"sms",
	L"roi", L"oem", L"kpi", L"ctr", L"cpc", L"cpm", L"cpa", L"erp", L"cdn", L"sso", L"otp",
	// Из «Переключать всегда» WinnyS (форум, 29.10.2026; остальное из его списка переключается и так).
	L"rdp", L"fs", L"ms", L"tnx", L"drweb",
	L"tbd", L"tba", L"wip", L"fyi", L"eta", L"asap", L"diy", L"afk", L"llc", L"inc", L"ltd", L"qty", L"pcs", L"pkg", L"aka",
	// Расширения файлов и зоны (форум, volodyalipkin и gutasiho, 31.10.2026: "иьз" - bmp; аудит 10.11.2026).
	L"bmp", L"webp", L"heic", L"avif", L"psd", L"cdr", L"dng", L"ogg", L"wma", L"mkv", L"avi", L"wmv", L"webm", L"mpg",
	L"vob", L"srt", L"xls", L"ppt", L"rtf", L"odp", L"epub", L"djvu", L"chm", L"tgz", L"zst", L"dmg", L"vhdx", L"vmdk",
	L"apk", L"apks", L"xapk", L"vbs", L"ini", L"bak", L"tmp", L"ttf", L"woff", L"htm", L"xhtml", L"aspx", L"jsp",
	L"zsh", L"sln", L"wasm", L"pyc", L"whl", L"ovpn",
	L"yml", L"toml", L"flac", L"tif", L"tiff", L"odt", L"ods", L"img", L"vhd", L"dat", L"otf", L"gz", L"xz",
	L"doc", L"log", L"bat", L"tar", L"bin", L"jar", L"raw", L"ico", L"aac", L"lua", L"mov", L"mpeg", L"opus",
	L"mobi", L"ova", L"ipa", L"deb", L"sys", L"conf", L"com", L"org", L"io", L"app", L"dev", L"pro",
	L"top", L"biz", L"xyz", L"info",
	// Повседневные, сетевые, железные и деловые аббревиатуры (аудит 10.11.2026).
	L"it", L"id", L"qr", L"sim", L"esim", L"pin", L"mac", L"vip", L"nat", L"mtu", L"ttl", L"qos", L"sni",
	L"vless", L"vmess", L"awg", L"nas", L"ups", L"psu", L"tws", L"anc", L"aux", L"dp", L"vga", L"dvi",
	L"ddr", L"raid", L"sata", L"pcie", L"dkim", L"dmarc", L"spf", L"imap", L"smtp", L"ldap", L"oidc",
	L"saml", L"totp", L"hmac", L"sha", L"aes", L"rsa", L"jwt", L"cors", L"grpc", L"crud", L"orm",
	L"ssr", L"csr", L"pwa", L"spa", L"waf", L"ddos", L"xss", L"csrf", L"cve", L"uuid", L"guid",
	L"regex", L"cron", L"jsonl", L"tsv", L"eof", L"crlf", L"utf", L"ascii", L"ansi", L"mqtt",
	L"wms", L"tms", L"pos", L"kyc", L"aml", L"nda", L"sla", L"awb", L"vat", L"cogs", L"ebitda",
	L"pnl", L"romi", L"cac", L"arpu", L"mrr", L"arr", L"yoy", L"qoq", L"ytd", L"moq", L"rrp",
	L"msrp", L"upc", L"ean", L"gtin", L"asin", L"tg" };
inline constexpr const wchar_t* kShortEnKeep[] = {
	L"ru", L"en", L"ua", L"uk", L"us", L"eu", L"de", L"fr", L"ok", L"cv", L"cif", L"fob", L"exw", L"fca", L"ddp",
	L"dap", L"cpt", L"cip", L"cfr", L"dpu", L"acc", L"nw", L"dep", L"yr", L"gn", L"dc", L"rd", L"vue", L"gdp",
	L"jal", L"rca", L"rnc", L"np", L"vnc", L"cnc", L"jvc" };
inline constexpr const wchar_t* kShortRu[] = {
	L"инн", L"ооо", L"оао", L"зао", L"пао", L"ип", L"ндс", L"ндфл", L"усн", L"огрн", L"огрнип", L"кпп", L"бик", L"снилс",
	L"упд", L"ттн", L"эдо", L"дрр", L"фнс", L"пфр", L"сфр", L"фсс", L"мвд", L"гибдд", L"жкх", L"рф", L"ссср", L"сша",
	L"ес", L"еаэс", L"оон", L"мкад", L"тц", L"цб", L"сбп", L"сдэк", L"ржд", L"вуз", L"сми", L"чп", L"тв", L"пк", L"гост",
	L"нк", L"тк", L"лк", L"кп", L"см", L"мм", L"км", L"кг", L"мг", L"шт", L"млн", L"млрд", L"тчк", L"зпт", L"спб", L"пн",
	L"вт", L"чт", L"пт", L"вс", L"кв", L"стр", L"вкл", L"мгц", L"ггц", L"кгц", L"мб", L"гб", L"кб", L"тб",
	L"днк", L"фио", L"мфц", L"эцп", L"укэп", L"мчд", L"егрюл", L"егрип", L"оквэд", L"окпо", L"кбк", L"коап",
	L"птс", L"дмс", L"втб", L"сбис", L"рбк", L"мчс", L"цска", L"ркн", L"мрот",
	L"ккт", L"тнвэд", L"егэ", L"огэ", L"бпла", L"загс", L"снип", L"санпин", L"фсин", L"фсо" };
inline constexpr const wchar_t* kShortRuKeep[] = { L"рк", L"мтс", L"стс", L"омс" };
// Сокращения с цифрами и через косую черту (аудит 10.11.2026: "ЬЗ3" - MP3, "И2И" - B2B, "ГЕА-8" - UTF-8, "ГШ.ГЧ" - UI/UX,
// ",|e" - б/у): закрытые списки, чтобы не трогать пароли, артикулы и "F4" ("А4").
inline constexpr const wchar_t* kAlnumEn[] = {
	L"mp3", L"mp4", L"m4a", L"fb2", L"ps1", L"woff2", L"b2b", L"b2c", L"d2c", L"p2p", L"2fa", L"x64", L"x86",
	L"utf-8", L"utf8", L"ipv4", L"ipv6", L"sha256", L"base64", L"win32", L"win64", L"arm64" };
inline constexpr const wchar_t* kSlashedEn[] = {
	L"ui/ux", L"ux/ui", L"tcp/ip", L"ci/cd", L"ar/vr", L"qa/qc" };
inline constexpr const wchar_t* kSlashedRu[] = {
	L"б/у", L"р/с", L"к/с", L"ж/д", L"п/п", L"в/ч", L"х/б" };
// Русские сокращения с точками ("и т.д." в английской раскладке - "b n/l/", "и т.п." - "b n/g/"; Дмитрий, 03.11.2026):
// точки - клавиша "/", набранное - не слово. Только с точками, как здесь: "lh" без точки - не "др".
inline constexpr const wchar_t* kDottedRu[] = { L"т.д.", L"т.п.", L"т.е.", L"т.к.", L"т.н.", L"т.ч.", L"т.о.", L"н.э.",
	L"и.о.", L"др." };

// ----- Слово перед числом (AutoSwitch::NumberFix) -----
// Там - версия ("мю1ю5" - v1.5), набранное - русское слово перед числом ("пю1" - п.1, "рисю2" - рис.2). Не длиннее
// четырёх букв: дальше движок не смотрит.
inline constexpr const wchar_t* kNumberEn[] = { L"v", L"ver", L"rc" };
inline constexpr const wchar_t* kNumberRu[] = { L"п", L"пп", L"ст", L"рис", L"табл", L"гл", L"ч", L"разд", L"стр", L"прил",
	L"т" };

// ----- Зоны сайтов и расширения файлов в конце адреса (AutoSwitch::Address) -----
inline constexpr const wchar_t* kZones[] = { L"ru", L"com", L"me", L"net", L"org", L"io", L"su", L"by", L"ua", L"kz", L"uz",
	L"info", L"biz", L"pro", L"app", L"dev", L"ai", L"tv", L"co", L"cc", L"to", L"gg", L"fm", L"ly", L"xyz", L"online",
	L"site", L"store", L"shop", L"tech", L"top", L"club", L"de", L"uk", L"us", L"eu", L"am", L"ge", L"md", L"kg", L"az",
	L"lv", L"lt", L"ee", L"pl", L"cz", L"fr", L"it", L"es", L"nl", L"jp", L"cn",
	L"jpg", L"jpeg", L"png", L"gif", L"bmp", L"svg", L"webp", L"tiff", L"tif", L"ico", L"heic", L"avif", L"psd", L"cdr",
	L"raw", L"dng", L"mp3", L"mp4", L"wav", L"flac", L"ogg", L"aac", L"wma", L"m4a", L"opus", L"mkv", L"avi", L"mov",
	L"wmv", L"webm", L"mpeg", L"mpg", L"srt", L"pdf", L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx", L"txt", L"csv",
	L"rtf", L"odt", L"ods", L"odp", L"epub", L"fb2", L"mobi", L"djvu", L"zip", L"rar", L"7z", L"tar", L"tgz", L"gz",
	L"xz", L"zst", L"iso", L"dmg", L"img", L"vhd", L"vhdx", L"vmdk", L"ova", L"exe", L"msi", L"dll", L"apk", L"apks",
	L"xapk", L"ipa", L"deb", L"rpm", L"bat", L"cmd", L"ps1", L"vbs", L"reg", L"sys", L"ini", L"cfg", L"conf", L"json",
	L"xml", L"yaml", L"yml", L"toml", L"env", L"log", L"bak", L"tmp", L"dat", L"bin", L"html", L"htm", L"css", L"js",
	L"ts", L"py", L"cpp", L"php", L"sql", L"sh", L"lua", L"jar", L"wasm", L"ttf", L"otf", L"woff", L"woff2", L"torrent" };

// ----- Короткие слова (ShortWords.h) -----
// Латиницей среди русских слов пишут и английские ok, lol: перед словом они не значат, что пишут по-английски.
inline constexpr const wchar_t* kNeutralEn[] = { L"ok", L"okay", L"oki", L"hi", L"lol", L"wow", L"omg", L"btw", L"thx",
	L"pls", L"plz" };
// Слова, которые знает словарь, которому можно верить, но так не пишут: английские короткие слова в русской раскладке
// (the - "еру", by - "ин", but - "иге", put - "зге"); в украинской эти клавиши дают те же буквы.
inline constexpr const wchar_t* kNotWordRu[] = { L"еру", L"ин", L"иге", L"вшу", L"пгн", L"шу", L"ль", L"дув", L"дуп",
	L"туе", L"зуе", L"зге", L"ен", L"фив" };

using Words = std::unordered_set<std::wstring>;

// Все списки сразу; слова - строчными. Имена - как ключи в rules.json.
struct Lists {
	Words shortEn, shortRu;     // short_en, short_ru
	Words keepEn, keepRu;       // keep_en, keep_ru
	Words dottedRu;             // dotted_ru
	Words alnumEn;              // встроенные с цифрами
	Words slashedEn, slashedRu; // встроенные через косую черту
	Words numberEn, numberRu;   // number_en, number_ru
	Words zones;                // zones
	Words neutralEn;            // neutral_en
	Words notWordRu;            // not_word_ru
	std::wstring version;     // версия файла правил; пусто - только встроенные
};

inline Lists Builtin() {
	auto set = [](const auto& list) { return Words(std::begin(list), std::end(list)); };
	Lists l;
	l.shortEn = set(kShortEn);
	l.shortRu = set(kShortRu);
	l.keepEn = set(kShortEnKeep);
	l.keepRu = set(kShortRuKeep);
	l.dottedRu = set(kDottedRu);
	l.alnumEn = set(kAlnumEn);
	l.slashedEn = set(kSlashedEn);
	l.slashedRu = set(kSlashedRu);
	l.numberEn = set(kNumberEn);
	l.numberRu = set(kNumberRu);
	l.zones = set(kZones);
	l.neutralEn = set(kNeutralEn);
	l.notWordRu = set(kNotWordRu);
	return l;
}

namespace detail {
inline std::atomic<const Lists*>& Pointer() {
	static std::atomic<const Lists*> p{ nullptr };
	return p;
}
// Все когда-либо выставленные списки - живут до конца программы.
inline std::vector<std::unique_ptr<const Lists>>& Kept() {
	static std::vector<std::unique_ptr<const Lists>> kept;
	return kept;
}
inline std::mutex& Lock() {
	static std::mutex m;
	return m;
}
}

// Выставить списки (из любого потока).
inline const Lists& Set(Lists lists) {
	std::lock_guard lock(detail::Lock());
	auto& kept = detail::Kept();
	kept.push_back(std::make_unique<const Lists>(std::move(lists)));
	detail::Pointer().store(kept.back().get(), std::memory_order_release);
	return *kept.back();
}

// Текущие списки; до первого Set - встроенные.
inline const Lists& Current() {
	if (const Lists* p = detail::Pointer().load(std::memory_order_acquire)) return *p;
	std::lock_guard lock(detail::Lock());
	if (const Lists* p = detail::Pointer().load(std::memory_order_acquire)) return *p;
	auto& kept = detail::Kept();
	kept.push_back(std::make_unique<const Lists>(Builtin()));
	detail::Pointer().store(kept.back().get(), std::memory_order_release);
	return *kept.back();
}

}
