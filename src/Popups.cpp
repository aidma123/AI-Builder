#include "Popups.hpp"

using namespace geode::prelude;

namespace ai_builder {

namespace {

std::string maskKey(std::string const& key) {
    if (key.size() <= 8) return "****";
    return key.substr(0, 7) + "..." + key.substr(key.size() - 4);
}

CCMenuItemSpriteExtra* makeButton(char const* text, CCObject* target, SEL_MenuHandler sel) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(.8f);
    return CCMenuItemSpriteExtra::create(spr, target, sel);
}

} // namespace

// ---------------------------------------------------------------- LinkPopup

LinkPopup* LinkPopup::create(Function<void()> onLinked) {
    auto ret = new LinkPopup();
    if (ret->init(std::move(onLinked))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool LinkPopup::init(Function<void()> onLinked) {
    if (!Popup::init(340.f, 220.f)) return false;
    m_onLinked = std::move(onLinked);

    this->setTitle("Link Claude");

    auto info = CCLabelBMFont::create(
        "Paste an API key from console.anthropic.com\nUsage is billed to that Anthropic account.",
        "bigFont.fnt"
    );
    info->setScale(.35f);
    info->setAlignment(kCCTextAlignmentCenter);
    m_mainLayer->addChildAtPosition(info, Anchor::Top, ccp(0, -55));

    m_input = TextInput::create(280.f, "sk-ant-...", "chatFont.fnt");
    m_input->setPasswordMode(true);
    m_input->setMaxCharCount(256);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Center, ccp(0, 5));

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_status->setScale(.35f);
    m_mainLayer->addChildAtPosition(m_status, Anchor::Center, ccp(0, -30));

    m_linkBtn = makeButton("Link", this, menu_selector(LinkPopup::onLink));
    m_buttonMenu->addChildAtPosition(m_linkBtn, Anchor::Bottom, ccp(-55, 28));

    auto getKey = makeButton("Get a key", this, menu_selector(LinkPopup::onGetKey));
    m_buttonMenu->addChildAtPosition(getKey, Anchor::Bottom, ccp(55, 28));

    m_unlinkBtn = makeButton("Unlink", this, menu_selector(LinkPopup::onUnlink));
    m_buttonMenu->addChildAtPosition(m_unlinkBtn, Anchor::Bottom, ccp(-55, 28));

    this->refresh();
    return true;
}

void LinkPopup::refresh() {
    auto key = getApiKey();
    m_linkBtn->setVisible(!key);
    m_unlinkBtn->setVisible(key.has_value());
    m_input->setVisible(!key);
    m_status->setColor({255, 255, 255});
    m_status->setString(key ? ("Linked: " + maskKey(*key)).c_str() : "Not linked");
}

void LinkPopup::onGetKey(CCObject*) {
    web::openLinkInBrowser("https://console.anthropic.com/settings/keys");
}

void LinkPopup::onUnlink(CCObject*) {
    clearApiKey();
    m_input->setString("");
    this->refresh();
}

void LinkPopup::onLink(CCObject*) {
    auto key = std::string(string::trim(m_input->getString()));
    if (key.empty()) {
        m_status->setColor({255, 120, 120});
        m_status->setString("Paste your API key first");
        return;
    }

    m_linkBtn->setEnabled(false);
    m_status->setColor({255, 255, 255});
    m_status->setString("Checking key...");

    m_request.spawn(verifyKey(key), [this, key](web::WebResponse res) {
        m_linkBtn->setEnabled(true);
        if (auto result = parseVerifyResponse(res); result.isErr()) {
            m_status->setColor({255, 120, 120});
            m_status->setString("Key rejected");
            FLAlertLayer::create("Link failed", result.unwrapErr(), "OK")->show();
            return;
        }
        setApiKey(key);
        this->refresh();
        m_status->setColor({120, 255, 120});
        if (m_onLinked) m_onLinked();
    });
}

// --------------------------------------------------------------- BuildPopup

BuildPopup* BuildPopup::create(Function<void(BuildPlan)> onPlan) {
    auto ret = new BuildPopup();
    if (ret->init(std::move(onPlan))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool BuildPopup::init(Function<void(BuildPlan)> onPlan) {
    if (!Popup::init(380.f, 250.f)) return false;
    m_onPlan = std::move(onPlan);

    this->setTitle("Build with Claude");

    m_input = TextInput::createMultiline(
        {330.f, 110.f},
        "e.g. a 1x speed cube section with three spike jumps, then a ship portal into a short ship corridor",
        "chatFont.fnt"
    );
    m_input->setMaxCharCount(2000);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Center, ccp(0, 15));

    m_status = CCLabelBMFont::create("Builds start at the centre of your screen.", "bigFont.fnt");
    m_status->setScale(.3f);
    m_mainLayer->addChildAtPosition(m_status, Anchor::Center, ccp(0, -55));

    m_spinner = LoadingSpinner::create(30.f);
    m_spinner->setVisible(false);
    m_mainLayer->addChildAtPosition(m_spinner, Anchor::Bottom, ccp(0, 30));

    m_buildBtn = makeButton("Build", this, menu_selector(BuildPopup::onBuild));
    m_buttonMenu->addChildAtPosition(m_buildBtn, Anchor::Bottom, ccp(-60, 28));

    auto account = makeButton("Account", this, menu_selector(BuildPopup::onAccount));
    m_buttonMenu->addChildAtPosition(account, Anchor::Bottom, ccp(60, 28));

    return true;
}

void BuildPopup::setBusy(bool busy) {
    m_buildBtn->setEnabled(!busy);
    m_buildBtn->setVisible(!busy);
    m_spinner->setVisible(busy);
    m_input->setEnabled(!busy);
}

void BuildPopup::onAccount(CCObject*) {
    LinkPopup::create()->show();
}

void BuildPopup::onBuild(CCObject*) {
    if (!getApiKey()) {
        LinkPopup::create([this] { m_status->setString("Linked! Press Build again."); })->show();
        return;
    }

    auto prompt = std::string(string::trim(m_input->getString()));
    if (prompt.empty()) {
        m_status->setString("Describe what you want built first.");
        return;
    }

    this->setBusy(true);
    m_status->setString("Claude is building... (this can take a minute)");

    m_request.spawn(requestBuild(prompt), [this](web::WebResponse res) {
        this->setBusy(false);
        auto result = parseBuildResponse(res);
        if (result.isErr()) {
            m_status->setString("Build failed.");
            FLAlertLayer::create("Build failed", result.unwrapErr(), "OK")->show();
            return;
        }
        auto plan = std::move(result).unwrap();
        // Copy the callback out first: onClose releases this popup.
        auto onPlan = std::move(m_onPlan);
        this->onClose(nullptr);
        if (onPlan) onPlan(std::move(plan));
    });
}

} // namespace ai_builder
