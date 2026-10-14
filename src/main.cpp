#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "SearchPopup.hpp"

using namespace geode::prelude;

class $modify(WebSearchEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto spr = CCSprite::createWithSpriteFrameName("gj_findBtn_001.png");
        spr->setScale(.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(WebSearchEditorUI::onWebSearch));
        btn->setID("web-search-button"_spr);

        // Sit next to the pause / settings buttons when Node IDs is installed,
        // otherwise use our own menu just below the pause button
        if (auto menu = this->getChildByID("settings-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        else {
            auto winSize = CCDirector::get()->getWinSize();
            auto menu = CCMenu::create();
            menu->setID("web-search-menu"_spr);
            menu->setPosition({ winSize.width - 25.f, winSize.height - 65.f });
            menu->addChild(btn);
            this->addChild(menu);
        }

        return true;
    }

    void onWebSearch(CCObject*) {
        SearchPopup::create()->show();
    }
};
