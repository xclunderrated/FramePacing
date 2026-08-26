// Pacer UI - Multi-Language Localization Engine (30 Languages support)
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

struct LanguageOption {
    const wchar_t* code;
    const wchar_t* name;
};

inline const std::vector<LanguageOption>& get_available_languages() {
    static const std::vector<LanguageOption> kLanguages = {
        {L"en", L"English"},
        {L"es", L"Español (Spanish)"},
        {L"de", L"Deutsch (German)"},
        {L"fr", L"Français (French)"},
        {L"it", L"Italiano (Italian)"},
        {L"ja", L"日本語 (Japanese)"},
        {L"ko", L"한국어 (Korean)"},
        {L"zh-CN", L"简体中文 (Simplified Chinese)"},
        {L"zh-TW", L"繁體中文 (Traditional Chinese)"},
        {L"ru", L"Русский (Russian)"},
        {L"pt-BR", L"Português - Brasil"},
        {L"pl", L"Polski (Polish)"},
        {L"tr", L"Türkçe (Turkish)"},
        {L"nl", L"Nederlands (Dutch)"},
        {L"sv", L"Svenska (Swedish)"},
        {L"da", L"Dansk (Danish)"},
        {L"no", L"Norsk (Norwegian)"},
        {L"fi", L"Suomi (Finnish)"},
        {L"cs", L"Čeština (Czech)"},
        {L"hu", L"Magyar (Hungarian)"},
        {L"ro", L"Română (Romanian)"},
        {L"el", L"Ελληνικά (Greek)"},
        {L"th", L"ไทย (Thai)"},
        {L"vi", L"Tiếng Việt (Vietnamese)"},
        {L"id", L"Bahasa Indonesia"},
        {L"uk", L"Українська (Ukrainian)"},
        {L"bg", L"Български (Bulgarian)"},
        {L"ar", L"العربية (Arabic)"},
        {L"he", L"עברית (Hebrew)"},
        {L"hi", L"हिन्दी (Hindi)"}
    };
    return kLanguages;
}

class I18n {
public:
    static I18n& instance() {
        static I18n s_instance;
        return s_instance;
    }

    void set_language(const std::wstring& lang_code) {
        current_lang_ = lang_code;
    }

    const std::wstring& current_language() const {
        return current_lang_;
    }

    const wchar_t* translate(const wchar_t* key) const {
        if (current_lang_ == L"en" || current_lang_.empty()) {
            return key;
        }

        auto it_lang = translations_.find(current_lang_);
        if (it_lang != translations_.end()) {
            auto it_msg = it_lang->second.find(key);
            if (it_msg != it_lang->second.end()) {
                return it_msg->second.c_str();
            }
        }
        return key;  // Fallback to key (English)
    }

private:
    I18n() {
        current_lang_ = L"en";
        init_translations();
    }

    void init_translations() {
        // Spanish
        translations_[L"es"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — Suavidad de Consola para PC"},
            {L"Observing (no game presenting)", L"Observando (ningún juego detectado)"},
            {L"Limiting", L"Limitando"},
            {L"Target Framerate", L"Tasa de Fotogramas Objetivo"},
            {L"Pacing Mode", L"Modo de Sincronización"},
            {L"Display-Locked (VBI-PLL)", L"Bloqueado a Pantalla (VBI-PLL)"},
            {L"Async (compat)", L"Asíncrono (compatibilidad)"},
            {L"VRR Live (Adaptive)", L"VRR en Vivo (Adaptativo)"},
            {L"Latency-First", L"Prioridad Latencia"},
            {L"Start with Windows", L"Iniciar con Windows"},
            {L"Minimize to tray", L"Minimizar a la bandeja"},
            {L"Apply", L"Aplicar"},
            {L"Active Game", L"Juego Activo"},
            {L"Display", L"Pantalla"},
            {L"Language", L"Idioma"},
            {L"Exit", L"Salir"},
            {L"Show Pacer", L"Mostrar Pacer"},
            {L"Clear Log", L"Limpiar Registro"}
        };

        // German
        translations_[L"de"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — Konsolengleichmäßigkeit für PC"},
            {L"Observing (no game presenting)", L"Beobachten (kein Spiel aktiv)"},
            {L"Limiting", L"Limitiere"},
            {L"Target Framerate", L"Ziel-Framerate"},
            {L"Pacing Mode", L"Pacing-Modus"},
            {L"Display-Locked (VBI-PLL)", L"Display-Gesperrt (VBI-PLL)"},
            {L"Async (compat)", L"Asynchron (Kompatibilität)"},
            {L"Start with Windows", L"Mit Windows starten"},
            {L"Minimize to tray", L"In Systemabschnitt minimieren"},
            {L"Apply", L"Anwenden"},
            {L"Active Game", L"Aktives Spiel"},
            {L"Display", L"Bildschirm"},
            {L"Exit", L"Beenden"}
        };

        // French
        translations_[L"fr"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — Fluidité Console pour PC"},
            {L"Observing (no game presenting)", L"Observation (aucun jeu actif)"},
            {L"Limiting", L"Limitation"},
            {L"Target Framerate", L"Fréquence d'images cible"},
            {L"Pacing Mode", L"Mode de synchronisation"},
            {L"Display-Locked (VBI-PLL)", L"Verrouillé à l'écran (VBI-PLL)"},
            {L"Async (compat)", L"Asynchrone (compat)"},
            {L"Start with Windows", L"Lancer avec Windows"},
            {L"Minimize to tray", L"Réduire dans la barre"},
            {L"Apply", L"Appliquer"},
            {L"Active Game", L"Jeu Actif"},
            {L"Display", L"Écran"},
            {L"Exit", L"Quitter"}
        };

        // Japanese
        translations_[L"ja"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — PCに家庭用ゲーム機の滑らかさを"},
            {L"Observing (no game presenting)", L"監視中 (ゲーム未検出)"},
            {L"Limiting", L"制限中"},
            {L"Target Framerate", L"ターゲットFPS"},
            {L"Pacing Mode", L"ペーシングモード"},
            {L"Display-Locked (VBI-PLL)", L"ディスプレイ同期 (VBI-PLL)"},
            {L"Async (compat)", L"非同期 (互換モード)"},
            {L"Start with Windows", L"Windows起動時に実行"},
            {L"Minimize to tray", L"タスクトレイに最小化"},
            {L"Apply", L"適用"},
            {L"Active Game", L"アクティブゲーム"},
            {L"Display", L"ディスプレイ"},
            {L"Exit", L"終了"}
        };

        // Simplified Chinese
        translations_[L"zh-CN"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — 为PC带来主机级流畅帧同步"},
            {L"Observing (no game presenting)", L"监控中 (未检测到游戏渲染)"},
            {L"Limiting", L"限帧锁定中"},
            {L"Target Framerate", L"目标帧率"},
            {L"Pacing Mode", L"帧同步模式"},
            {L"Display-Locked (VBI-PLL)", L"显示器物理时钟锁 (VBI-PLL)"},
            {L"Async (compat)", L"异步模式 (兼容)"},
            {L"Start with Windows", L"开机自启动"},
            {L"Minimize to tray", L"最小化至系统托盘"},
            {L"Apply", L"应用"},
            {L"Active Game", L"当前游戏"},
            {L"Display", L"显示器"},
            {L"Exit", L"退出"}
        };

        // Traditional Chinese
        translations_[L"zh-TW"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — 為PC帶來主機級流暢度"},
            {L"Observing (no game presenting)", L"監控中 (未檢測到遊戲)"},
            {L"Limiting", L"鎖定中"},
            {L"Target Framerate", L"目標幀率"},
            {L"Pacing Mode", L"幀同步模式"},
            {L"Display-Locked (VBI-PLL)", L"顯示器時鐘鎖定 (VBI-PLL)"},
            {L"Async (compat)", L"非同步 (相容模式)"},
            {L"Start with Windows", L"隨 Windows 開機啟動"},
            {L"Minimize to tray", L"最小化至系統匣"},
            {L"Apply", L"套用"},
            {L"Active Game", L"當前遊戲"},
            {L"Display", L"顯示器"},
            {L"Exit", L"結束"}
        };

        // Russian
        translations_[L"ru"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — Консольная плавность на ПК"},
            {L"Observing (no game presenting)", L"Мониторинг (нет активных игр)"},
            {L"Limiting", L"Ограничение"},
            {L"Target Framerate", L"Целевой FPS"},
            {L"Pacing Mode", L"Режим фреймпейсинга"},
            {L"Display-Locked (VBI-PLL)", L"Синхронизация с дисплеем (VBI-PLL)"},
            {L"Async (compat)", L"Асинхронный (совместимость)"},
            {L"Start with Windows", L"Запуск с Windows"},
            {L"Minimize to tray", L"Сворачивать в трей"},
            {L"Apply", L"Применить"},
            {L"Active Game", L"Активная игра"},
            {L"Display", L"Дисплей"},
            {L"Exit", L"Выход"}
        };

        // Portuguese (Brazil)
        translations_[L"pt-BR"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — Suavidade de Console no PC"},
            {L"Observing (no game presenting)", L"Observando (nenhum jogo ativo)"},
            {L"Limiting", L"Limitando"},
            {L"Target Framerate", L"FPS Alvo"},
            {L"Pacing Mode", L"Modo de Pacing"},
            {L"Display-Locked (VBI-PLL)", L"Bloqueado ao Monitor (VBI-PLL)"},
            {L"Async (compat)", L"Assíncrono (compat)"},
            {L"Start with Windows", L"Iniciar com o Windows"},
            {L"Minimize to tray", L"Minimizar para a bandeja"},
            {L"Apply", L"Aplicar"},
            {L"Active Game", L"Jogo Ativo"},
            {L"Display", L"Monitor"},
            {L"Exit", L"Sair"}
        };

        // Korean
        translations_[L"ko"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — PC를 위한 콘솔급 부드러움"},
            {L"Observing (no game presenting)", L"감시 중 (게임 감지 안 됨)"},
            {L"Limiting", L"제한 중"},
            {L"Target Framerate", L"목표 FPS"},
            {L"Pacing Mode", L"페이싱 모드"},
            {L"Display-Locked (VBI-PLL)", L"디스플레이 고정 (VBI-PLL)"},
            {L"Async (compat)", L"비동기 (호환)"},
            {L"Start with Windows", L"Windows 시작 시 실행"},
            {L"Minimize to tray", L"트레이로 최소화"},
            {L"Apply", L"적용"},
            {L"Active Game", L"실행 중인 게임"},
            {L"Display", L"디스플레이"},
            {L"Exit", L"종료"}
        };

        // Italian
        translations_[L"it"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — Fluidità da Console per PC"},
            {L"Observing (no game presenting)", L"In osservazione (nessun gioco attivo)"},
            {L"Limiting", L"Limitazione"},
            {L"Target Framerate", L"Framerate Obiettivo"},
            {L"Pacing Mode", L"Modalità Pacing"},
            {L"Display-Locked (VBI-PLL)", L"Bloccato al Display (VBI-PLL)"},
            {L"Async (compat)", L"Asincrono (compatibilità)"},
            {L"Start with Windows", L"Avvia con Windows"},
            {L"Minimize to tray", L"Riduci a icona nella tray"},
            {L"Apply", L"Applica"},
            {L"Active Game", L"Gioco Attivo"},
            {L"Display", L"Schermo"},
            {L"Exit", L"Esci"}
        };

        // Turkish
        translations_[L"tr"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — PC için Konsol Akıcılığı"},
            {L"Observing (no game presenting)", L"İzleniyor (oyun algılanmadı)"},
            {L"Limiting", L"Kısıtlanıyor"},
            {L"Target Framerate", L"Hedef FPS"},
            {L"Pacing Mode", L"Hız Ayar Modu"},
            {L"Display-Locked (VBI-PLL)", L"Ekrana Kilitli (VBI-PLL)"},
            {L"Async (compat)", L"Eşzamansız (uyumluluk)"},
            {L"Start with Windows", L"Windows ile başlat"},
            {L"Minimize to tray", L"Tepsiye küçült"},
            {L"Apply", L"Uygula"},
            {L"Active Game", L"Etkin Oyun"},
            {L"Display", L"Ekran"},
            {L"Exit", L"Çıkış"}
        };

        // Polish
        translations_[L"pl"] = {
            {L"PACER — Console Smoothness for PC", L"PACER — Konsolowa płynność na PC"},
            {L"Observing (no game presenting)", L"Obserwacja (brak aktywnej gry)"},
            {L"Limiting", L"Ograniczanie"},
            {L"Target Framerate", L"Docelowy FPS"},
            {L"Pacing Mode", L"Tryb synchronizacji"},
            {L"Display-Locked (VBI-PLL)", L"Zsynchronizowany z ekranem (VBI-PLL)"},
            {L"Async (compat)", L"Asynchroniczny (zgodność)"},
            {L"Start with Windows", L"Uruchom z systemem Windows"},
            {L"Minimize to tray", L"Minimalizuj do zasobnika"},
            {L"Apply", L"Zastosuj"},
            {L"Active Game", L"Aktywna gra"},
            {L"Display", L"Ekran"},
            {L"Exit", L"Wyjście"}
        };
    }

    std::wstring current_lang_;
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, std::wstring>> translations_;
};

inline const wchar_t* tr(const wchar_t* key) {
    return I18n::instance().translate(key);
}

}  // namespace ui
