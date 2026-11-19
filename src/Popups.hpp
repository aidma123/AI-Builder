#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include "Claude.hpp"

namespace ai_builder {

// Paste-an-API-key popup. Verifies the key against the API before saving it.
class LinkPopup : public geode::Popup {
protected:
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_linkBtn = nullptr;
    CCMenuItemSpriteExtra* m_unlinkBtn = nullptr;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    geode::Function<void()> m_onLinked;

    bool init(geode::Function<void()> onLinked);
    void refresh();
    void onLink(cocos2d::CCObject*);
    void onUnlink(cocos2d::CCObject*);
    void onGetKey(cocos2d::CCObject*);

public:
    static LinkPopup* create(geode::Function<void()> onLinked = nullptr);
};

// Describe-what-to-build popup. Hands the finished plan to `onPlan`.
class BuildPopup : public geode::Popup {
protected:
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_buildBtn = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    geode::Function<void(BuildPlan)> m_onPlan;

    bool init(geode::Function<void(BuildPlan)> onPlan);
    void setBusy(bool busy);
    void onBuild(cocos2d::CCObject*);
    void onAccount(cocos2d::CCObject*);

public:
    static BuildPopup* create(geode::Function<void(BuildPlan)> onPlan);
};

} // namespace ai_builder
