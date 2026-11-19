#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "Claude.hpp"
#include "Popups.hpp"

#include <cmath>
#include <map>
#include <set>
#include <unordered_set>

using namespace geode::prelude;
using namespace ai_builder;

namespace {

constexpr float GRID = 30.f;
// Centre of a block resting on the ground, in object-layer coordinates.
// (The ground line sits at y = 90 in the editor's object layer.)
constexpr float GROUND_ROW_CENTER = 105.f;

} // namespace

class $modify(AIEditorUI, EditorUI) {
    // Whether a trigger property holds a group ID. The pulse trigger's
    // target (51) is a colour channel unless its target type (52) is 1.
    static bool propTargetsGroup(PlacedObject const& o, int key) {
        if (!isGroupPropKey(key)) return false;
        if (o.id == 1006 && key == 51) {
            for (auto const& [k, v] : o.props) {
                if (k == 52) return static_cast<int>(v) == 1;
            }
            return false;
        }
        return true;
    }

    static std::string formatValue(double value) {
        if (std::abs(value - std::round(value)) < 1e-6) {
            return std::to_string(static_cast<long long>(std::llround(value)));
        }
        return fmt::format("{:.3f}", value);
    }

    // Claude numbers its groups 1, 2, 3... Map each to a group that isn't
    // used anywhere in the level yet, so builds never hijack existing groups.
    std::map<int, int> allocateGroups(BuildPlan const& plan) {
        std::set<int> local;
        for (auto const& o : plan.objects) {
            for (int g : o.groups) local.insert(g);
            for (auto const& [key, value] : o.props) {
                if (propTargetsGroup(o, key) && value >= 1) local.insert(static_cast<int>(value));
            }
        }

        std::map<int, int> result;
        std::unordered_set<int> taken;
        for (int g : local) {
            int real = m_editorLayer->getNextFreeGroupID(taken);
            if (real <= 0 || real > 9999) real = g; // editor gave us nothing usable
            taken.insert(real);
            result[g] = real;
        }
        return result;
    }
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto spr = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_starsIcon_001.png", 1.f, CircleBaseColor::Green, CircleBaseSize::Small
        );
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIEditorUI::onAIBuild));
        btn->setID("ai-build-button"_spr);

        // Sit next to undo/redo/delete if node-ids gave us that menu,
        // otherwise make a small menu of our own in the top left.
        if (auto undoMenu = this->getChildByID("undo-menu")) {
            undoMenu->addChild(btn);
            undoMenu->updateLayout();
        } else {
            auto winSize = CCDirector::get()->getWinSize();
            auto menu = CCMenu::create();
            menu->setID("ai-build-menu"_spr);
            menu->setPosition({120.f, winSize.height - 60.f});
            menu->addChild(btn);
            this->addChild(menu);
        }
        return true;
    }

    void onAIBuild(CCObject*) {
        WeakRef<EditorUI> self = this;
        BuildPopup::create([self](BuildPlan plan) {
            auto ui = self.lock();
            if (!ui) return;
            static_cast<AIEditorUI*>(ui.data())->placePlan(plan);
        })->show();
    }

    void placePlan(BuildPlan const& plan) {
        auto layer = m_editorLayer;
        if (!layer || !layer->m_objectLayer) return;

        // Build origin: the grid column at the middle of the screen.
        auto winSize = CCDirector::get()->getWinSize();
        auto center = layer->m_objectLayer->convertToNodeSpace(ccp(winSize.width / 2, winSize.height / 2));
        float originX = std::max(0.f, std::floor(center.x / GRID) * GRID);

        auto expectedPos = [&](PlacedObject const& o) {
            return CCPoint {
                originX + static_cast<float>(o.col) * GRID + GRID / 2,
                GROUND_ROW_CENTER + static_cast<float>(o.row) * GRID
            };
        };

        auto groupMap = this->allocateGroups(plan);
        auto realGroup = [&](int local) {
            auto it = groupMap.find(local);
            return it != groupMap.end() ? it->second : local;
        };

        // Level-string coordinates drop the 90-unit ground offset.
        std::string str;
        for (auto const& o : plan.objects) {
            auto pos = expectedPos(o);
            str += fmt::format("1,{},2,{:.2f},3,{:.2f}", o.id, pos.x, pos.y - 90.f);
            if (o.flipX) str += ",4,1";
            if (o.flipY) str += ",5,1";
            if (std::abs(o.rotation) > 0.01) str += fmt::format(",6,{:.2f}", o.rotation);
            if (std::abs(o.scale - 1.0) > 0.001) str += fmt::format(",32,{:.3f}", o.scale);
            if (o.colorChannel > 0) str += fmt::format(",21,{}", o.colorChannel);
            if (o.zOrder != 0) str += fmt::format(",25,{}", o.zOrder);

            if (!o.groups.empty()) {
                std::vector<std::string> ids;
                for (int g : o.groups) ids.push_back(std::to_string(realGroup(g)));
                str += ",57," + string::join(ids, ".");
            }

            for (auto const& [key, value] : o.props) {
                if (propTargetsGroup(o, key)) {
                    str += fmt::format(",{},{}", key, realGroup(static_cast<int>(value)));
                } else {
                    str += fmt::format(",{},{}", key, formatValue(value));
                }
            }
            str += ";";
        }

        auto created = layer->createObjectsFromString(str, false, true);
        if (!created || created->count() == 0) {
            FLAlertLayer::create("Build failed", "The editor didn't create any objects.", "OK")->show();
            return;
        }

        // Sanity-check where the editor actually put the first object and
        // shift everything if the coordinate conventions disagree.
        auto first = static_cast<GameObject*>(created->objectAtIndex(0));
        auto want = expectedPos(plan.objects.front());
        auto delta = want - first->getPosition();
        if (std::abs(delta.x) > 1.f || std::abs(delta.y) > 1.f) {
            log::warn("Placed objects were off by ({}, {}), correcting", delta.x, delta.y);
            for (auto obj : CCArrayExt<GameObject*>(created)) {
                this->moveObject(obj, delta);
            }
        }

        this->selectObjects(created, true);
        this->updateButtons();
        this->updateObjectInfoLabel();

        auto msg = fmt::format("Placed {} objects", created->count());
        if (!plan.summary.empty()) msg += ": " + plan.summary;
        Notification::create(msg, NotificationIcon::Success, 4.f)->show();
    }
};
