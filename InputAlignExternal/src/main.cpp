// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#include "Runtime.hpp"
#include "Shortcut.hpp"
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>

using namespace geode::prelude;
namespace {
std::shared_ptr<iax::Runtime> current(GJBaseGameLayer* layer) {
    auto runtime = iax::active;
    return runtime && runtime->owner == layer ? runtime : nullptr;
}
CCPoint corner(std::string const& name, CCSize size, float margin) {
    return {name.find("der.") != std::string::npos ? size.width - margin : margin,
            name.find("Arriba") != std::string::npos ? size.height - margin : margin};
}
}

class $modify(IAXGame, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        auto runtime = current(this);
        if (runtime && runtime->input(down, button, player1)) return;
        GJBaseGameLayer::handleButton(down, button, player1);
        if (runtime && button == 1 && player1) runtime->applied(down);
    }
    void processCommands(float dt, bool half, bool last) {
        auto runtime = current(this);
        if (runtime) runtime->before(dt, half);
        GJBaseGameLayer::processCommands(dt, half, last);
        if (runtime) runtime->after();
    }
    void update(float dt) {
        auto runtime = current(this);
        if (runtime && m_isPracticeMode) dt *= runtime->settings.speed;
        GJBaseGameLayer::update(dt);
    }
};

class $modify(IAXPlayer, PlayerObject) {
    void update(float dt) {
        auto runtime = iax::active;
        bool primary = runtime && runtime->owner->m_player1 == this;
        bool held = primary && runtime->observedHeld();
        auto session = primary ? runtime->session : 0;
        PlayerObject::update(dt);
        if (primary && runtime->session == session) runtime->physicsStep(dt, held);
    }
};

class $modify(IAXPlay, PlayLayer) {
    struct Fields {
        CCLabelBMFont* hud = nullptr;
        iax::Clock::time_point lastFrame = iax::Clock::now(), refreshAt = iax::Clock::now();
        float fps = 0;
        bool trailHidden = false, trailWasVisible = true;
    };
    bool init(GJGameLevel* level, bool replay, bool noObjects) {
        if (!PlayLayer::init(level, replay, noObjects)) return false;
        auto runtime = std::make_shared<iax::Runtime>(this);
        iax::active = runtime;
        try {
            if (!runtime->start(std::string(level->m_levelString)))
                log::warn("Input Align External: puente no disponible; se conservan los clics");
        } catch (std::exception const& error) {
            runtime->settings.enabled = false;
            runtime->status = "No se pudo preparar el puente";
            log::warn("Input Align External: {}", error.what());
        }
        m_fields->hud = CCLabelBMFont::create("", "bigFont.fnt");
        m_fields->hud->setID("input-align-hud");
        addChild(m_fields->hud, 1000);
        return true;
    }
    void resetLevel() {
        auto runtime = current(this);
        if (runtime) runtime->reset();
        PlayLayer::resetLevel();
        if (runtime) {
            runtime->held = runtime->observedHeld();
            runtime->refresh();
        }
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto runtime = current(this);
        if (runtime && runtime->settings.noclip && m_isPracticeMode && object != m_anticheatSpike) {
            ++runtime->noclipDeaths;
            return;
        }
        if (runtime) { runtime->gate.clear(); runtime->flushRecording(); }
        PlayLayer::destroyPlayer(player, object);
    }
    void levelComplete() {
        if (auto runtime = current(this)) { runtime->gate.clear(); runtime->flushRecording(); }
        PlayLayer::levelComplete();
    }
    void onQuit() {
        if (auto runtime = current(this)) { runtime->flushRecording(); iax::active.reset(); }
        PlayLayer::onQuit();
    }
    void onExit() {
        if (auto runtime = current(this)) { runtime->flushRecording(); iax::active.reset(); }
        PlayLayer::onExit();
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto runtime = current(this);
        if (!runtime || !m_fields->hud) return;
        auto now = iax::Clock::now();
        float seconds = std::chrono::duration<float>(now - m_fields->lastFrame).count();
        m_fields->lastFrame = now;
        if (seconds > 0 && seconds < 1) m_fields->fps = m_fields->fps * .9f + .1f / seconds;
        if (now - m_fields->refreshAt >= std::chrono::milliseconds(250)) {
            m_fields->refreshAt = now;
            runtime->refresh();
            while (!runtime->clickTimes.empty() && now - runtime->clickTimes.front() > std::chrono::seconds(1))
                runtime->clickTimes.pop_front();
            std::string text;
            auto append = [&text](std::string value) { if (!text.empty()) text += "\n"; text += value; };
            auto mod = Mod::get();
            if (mod->getSettingValue<bool>("hud-fps")) append(fmt::format("FPS: {:.0f}", m_fields->fps));
            if (mod->getSettingValue<bool>("hud-cps")) append(fmt::format("CPS: {}", runtime->clickTimes.size()));
            if (mod->getSettingValue<bool>("hud-clicks")) append(fmt::format("Clicks: {}", runtime->clicks));
            if (mod->getSettingValue<bool>("hud-saved")) append(fmt::format("Corrections: {}", runtime->corrected));
            if (mod->getSettingValue<bool>("hud-noclip-deaths")) append(fmt::format("Noclip: {}", runtime->noclipDeaths));
            auto label = m_fields->hud;
            label->setString(text.c_str()); label->setVisible(!text.empty());
            label->setScale(mod->getSettingValue<int64_t>("hud-scale") / 200.f);
            label->setOpacity(static_cast<GLubyte>(mod->getSettingValue<int64_t>("hud-opacity") * 255 / 100));
            auto name = mod->getSettingValue<std::string>("hud-corner");
            label->setAnchorPoint({name.find("der.") != std::string::npos ? 1.f : 0.f,
                                   name.find("Arriba") != std::string::npos ? 1.f : 0.f});
            label->setPosition(corner(name, CCDirector::sharedDirector()->getWinSize(), 8));
        }
        if (m_player1 && m_player1->m_waveTrail) {
            bool hide = runtime->settings.noTrail || runtime->settings.performance;
            if (hide && !m_fields->trailHidden) m_fields->trailWasVisible = m_player1->m_waveTrail->isVisible();
            if (hide) m_player1->m_waveTrail->setVisible(false);
            else if (m_fields->trailHidden) m_player1->m_waveTrail->setVisible(m_fields->trailWasVisible);
            m_fields->trailHidden = hide;
        }
    }
};

class $modify(IAXPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (iax::active) { iax::active->refresh(); iax::addPauseButton(this); }
    }
};

// Shift+Tab toggles correction only while a level runtime is active.
class $modify(IAXKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        static iax::Shortcut shortcut;
        auto result = shortcut.handle(key == KEY_Tab, down, repeat,
                                               iax::active && getShiftKeyPressed());
        if (result.toggle && iax::active) {
            auto mod = Mod::get();
            mod->setSettingValue<bool>("enabled", !mod->getSettingValue<bool>("enabled"));
            iax::active->refresh();
        }
        if (result.consume) return true;
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};
