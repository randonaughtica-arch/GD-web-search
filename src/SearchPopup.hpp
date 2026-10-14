#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/utils/async.hpp>
#include "Search.hpp"

class SearchPopup : public geode::Popup {
protected:
    SearchSource m_source;
    std::vector<SearchResult> m_results;

    geode::TextInput* m_input = nullptr;
    ButtonSprite* m_sourceSpr = nullptr;
    geode::ScrollLayer* m_list = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_searchTask;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_articleTask;

    bool init(std::string const& query);
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;

    void onSearch(cocos2d::CCObject*);
    void onSwitchSource(cocos2d::CCObject*);
    void onOpenInBrowser(cocos2d::CCObject*);
    void onRead(cocos2d::CCObject*);
    void onOpenResult(cocos2d::CCObject*);
    void onCopyResult(cocos2d::CCObject*);

    void search();
    void setStatus(std::string const& text, bool loading = false);
    void showResults();
    cocos2d::CCNode* createCell(SearchResult const& result, int index, float width);
    SearchResult const* resultFor(cocos2d::CCObject* sender) const;

public:
    static SearchPopup* create(std::string const& query = "");

    // The last query is remembered between popups while the game is open
    static inline std::string s_lastQuery;
};
