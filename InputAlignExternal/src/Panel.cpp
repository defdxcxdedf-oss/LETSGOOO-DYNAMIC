// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#include "Runtime.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <algorithm>

using namespace geode::prelude;
namespace iax {
class Panel : public Popup {
    CCLabelBMFont* m_status = nullptr;
    Clock::time_point m_opened = Clock::now();
    bool m_dragging = false, m_english = false;
    CCPoint m_dragStart{};
    void onSettings(CCObject*) { m_opened = Clock::now(); openSettingsPopup(Mod::get()); }
    void onLaunch(CCObject*) { m_opened = Clock::now(); if (active) active->launchWorker(); }
    void onFolder(CCObject*) { m_opened = Clock::now(); if (active) geode::utils::file::openFolder(active->directory()); }
    void onLanguage(CCObject*) {
        Mod::get()->setSettingValue<std::string>("language", m_english ? "Español" : "English");
        onClose(nullptr); showPanel();
    }
    void poll(float) {
        if (!active) { onClose(nullptr); return; }
        std::string text = active->status;
        text += active->settings.enabled ? "\nON | Shift+Tab" : "\nOFF | Shift+Tab";
        if (active->cbfPresent) text += m_english ? "\nCBF: correction suspended" : "\nCBF: correccion suspendida";
        if (active->conflict) text += m_english ? "\nDisable original InputAlign" : "\nDesactiva el InputAlign original";
        text += fmt::format("\n{}: {}   {}: {}", m_english ? "Clicks" : "Clics", active->clicks,
                            m_english ? "Corrections" : "Correcciones", active->corrected);
        m_status->setString(text.c_str());
        auto seconds = Mod::get()->getSettingValue<int64_t>("auto-close");
        if (seconds > 0 && Clock::now() - m_opened >= std::chrono::seconds(seconds)) onClose(nullptr);
    }
    void button(char const* title, CCPoint position, SEL_MenuHandler handler) {
        auto sprite = ButtonSprite::create(title);
        sprite->setScale(.65f);
        auto item = CCMenuItemSpriteExtra::create(sprite, this, handler);
        item->setPosition(position);
        m_buttonMenu->addChild(item);
    }
    bool setup() {
        if (!Popup::init(380, 260)) return false;
        auto mod = Mod::get();
        m_english = mod->getSettingValue<std::string>("language") == "English";
        setTitle("Input Align External");
        auto color = mod->getSettingValue<ccColor3B>("menu-color");
        m_bgSprite->setColor(color);
        m_bgSprite->setOpacity(static_cast<GLubyte>(mod->getSettingValue<int64_t>("menu-opacity") * 255 / 100));
        m_bgSprite->setZOrder(-10);
        m_mainLayer->setScale(mod->getSettingValue<int64_t>("menu-scale") / 100.f);
        auto image = mod->getSettingValue<std::filesystem::path>("background-image");
        std::error_code error;
        if (!image.empty() && std::filesystem::file_size(image, error) <= 4 * 1024 * 1024 && !error) {
            if (auto sprite = CCSprite::create(image.string().c_str())) {
                auto size = sprite->getContentSize();
                if (size.width > 0 && size.height > 0) {
                    sprite->setScale(std::min(370.f / size.width, 250.f / size.height));
                    sprite->setPosition({190, 130});
                    sprite->setOpacity(static_cast<GLubyte>(mod->getSettingValue<int64_t>("background-opacity") * 255 / 100));
                    m_mainLayer->addChild(sprite, -9);
                    auto dim = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(mod->getSettingValue<int64_t>("background-dim") * 255 / 100)}, 370, 250);
                    dim->setPosition({5, 5}); m_mainLayer->addChild(dim, -8);
                }
            }
        }
        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setScale(.6f); m_status->setPosition({190, 178});
        m_mainLayer->addChild(m_status);
        auto info = CCLabelBMFont::create(m_english ? "Experimental | Windows GD 2.2081\nLaunch the console, then restart the attempt."
                                                    : "Experimental | Windows GD 2.2081\nAbre la consola y reinicia el intento.", "chatFont.fnt");
        info->setScale(.6f); info->setPosition({190, 118}); m_mainLayer->addChild(info);
        button(m_english ? "Open CMD" : "Abrir CMD", {105, 72}, menu_selector(Panel::onLaunch));
        button(m_english ? "Settings" : "Ajustes", {270, 72}, menu_selector(Panel::onSettings));
        button(m_english ? "Data folder" : "Carpeta de datos", {115, 32}, menu_selector(Panel::onFolder));
        button(m_english ? "Espanol" : "English", {280, 32}, menu_selector(Panel::onLanguage));
        schedule(schedule_selector(Panel::poll), .25f);
        return true;
    }
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        m_opened = Clock::now();
        auto point = m_mainLayer->convertToNodeSpace(touch->getLocation());
        if (!Mod::get()->getSettingValue<bool>("menu-lock") && CCRect(35, 220, 310, 40).containsPoint(point)) {
            m_dragging = true; m_dragStart = touch->getLocation(); return true;
        }
        return Popup::ccTouchBegan(touch, event);
    }
    void ccTouchMoved(CCTouch* touch, CCEvent* event) override {
        if (!m_dragging) return Popup::ccTouchMoved(touch, event);
        m_opened = Clock::now();
        auto location = touch->getLocation();
        m_mainLayer->setPosition(m_mainLayer->getPosition() + location - m_dragStart);
        m_dragStart = location;
    }
    void ccTouchEnded(CCTouch* touch, CCEvent* event) override {
        m_dragging = false; Popup::ccTouchEnded(touch, event);
    }
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override {
        m_dragging = false; Popup::ccTouchCancelled(touch, event);
    }
public:
    static Panel* create() {
        auto panel = new Panel;
        if (panel->setup()) { panel->autorelease(); return panel; }
        delete panel; return nullptr;
    }
};
class PauseButton : public CCNode {
public:
    void clicked(CCObject*) { showPanel(); }
};
void showPanel() { if (active) if (auto panel = Panel::create()) panel->show(); }
void addPauseButton(PauseLayer* pause) {
    auto target = new PauseButton; target->autorelease(); pause->addChild(target);
    auto sprite = ButtonSprite::create("IAX");
    auto mod = Mod::get();
    sprite->setScale(static_cast<float>(mod->getSettingValue<double>("button-scale")));
    sprite->setOpacity(static_cast<GLubyte>(mod->getSettingValue<int64_t>("button-opacity") * 255 / 100));
    auto item = CCMenuItemSpriteExtra::create(sprite, target, menu_selector(PauseButton::clicked));
    auto menu = CCMenu::create(); menu->setPosition({0, 0});
    auto size = CCDirector::sharedDirector()->getWinSize();
    auto name = mod->getSettingValue<std::string>("button-corner");
    item->setPosition({name.find("der.") != std::string::npos ? size.width - 35 : 35,
                       name.find("Arriba") != std::string::npos ? size.height - 25 : 25});
    menu->addChild(item); pause->addChild(menu, 1000);
}
}
