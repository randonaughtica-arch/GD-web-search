#include "SearchPopup.hpp"

#include <Geode/ui/MDPopup.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/TextArea.hpp>

using namespace geode::prelude;

namespace {
    constexpr float POPUP_WIDTH = 440.f;
    constexpr float POPUP_HEIGHT = 290.f;
    constexpr float LIST_WIDTH = 400.f;
    constexpr float LIST_HEIGHT = 190.f;
    constexpr float CELL_PADDING = 6.f;
    constexpr float BUTTON_COLUMN_WIDTH = 64.f;

    std::optional<SearchSource> s_lastSource;

    ButtonSprite* smallButton(char const* text, char const* bg = "GJ_button_01.png") {
        auto spr = ButtonSprite::create(text, 60, true, "bigFont.fnt", bg, 30.f, .6f);
        spr->setScale(.6f);
        return spr;
    }
}

SearchPopup* SearchPopup::create(std::string const& query) {
    auto ret = new SearchPopup();
    if (ret->init(query)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SearchPopup::init(std::string const& query) {
    if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

    this->setTitle("Web Search");
    m_source = s_lastSource.value_or(
        search::sourceFromString(Mod::get()->getSettingValue<std::string>("default-source"))
    );

    // Top row: [source] [query input] [search] [web]
    m_sourceSpr = ButtonSprite::create(search::sourceName(m_source), 110, true, "bigFont.fnt", "GJ_button_04.png", 30.f, .5f);
    m_sourceSpr->setScale(.7f);
    auto sourceBtn = CCMenuItemSpriteExtra::create(m_sourceSpr, this, menu_selector(SearchPopup::onSwitchSource));
    m_buttonMenu->addChildAtPosition(sourceBtn, Anchor::Top, ccp(-163, -50));

    m_input = TextInput::create(210.f, "Search...");
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setMaxCharCount(200);
    m_input->setString(query.empty() ? s_lastQuery : query);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Top, ccp(-5, -50));

    auto searchSpr = CCSprite::createWithSpriteFrameName("gj_findBtn_001.png");
    searchSpr->setScale(.75f);
    auto searchBtn = CCMenuItemSpriteExtra::create(searchSpr, this, menu_selector(SearchPopup::onSearch));
    m_buttonMenu->addChildAtPosition(searchBtn, Anchor::Top, ccp(122, -50));

    auto webSpr = ButtonSprite::create("Web", 50, true, "bigFont.fnt", "GJ_button_02.png", 30.f, .6f);
    webSpr->setScale(.7f);
    auto webBtn = CCMenuItemSpriteExtra::create(webSpr, this, menu_selector(SearchPopup::onOpenInBrowser));
    m_buttonMenu->addChildAtPosition(webBtn, Anchor::Top, ccp(175, -50));

    // Results list
    auto listOffset = ccp(-LIST_WIDTH / 2, -LIST_HEIGHT / 2 - 25.f);

    auto listBG = CCLayerColor::create({ 0, 0, 0, 90 }, LIST_WIDTH, LIST_HEIGHT);
    listBG->ignoreAnchorPointForPosition(false);
    listBG->setAnchorPoint({ 0, 0 });
    m_mainLayer->addChildAtPosition(listBG, Anchor::Center, listOffset);

    m_list = ScrollLayer::create({ LIST_WIDTH, LIST_HEIGHT });
    m_mainLayer->addChildAtPosition(m_list, Anchor::Center, listOffset);

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_status->setScale(.45f);
    m_mainLayer->addChildAtPosition(m_status, Anchor::Center, ccp(0, -25));

    m_spinner = LoadingSpinner::create(40.f);
    m_spinner->setVisible(false);
    m_mainLayer->addChildAtPosition(m_spinner, Anchor::Center, ccp(0, -5));

    if (m_input->getString().empty()) {
        this->setStatus("Type something and press search!");
        m_input->focus();
    }
    else {
        this->search();
    }

    handleTouchPriority(this);
    return true;
}

void SearchPopup::keyDown(enumKeyCodes key, double timestamp) {
    if (key == KEY_Enter) {
        m_input->defocus();
        this->search();
        return;
    }
    Popup::keyDown(key, timestamp);
}

void SearchPopup::onSearch(CCObject*) {
    m_input->defocus();
    this->search();
}

void SearchPopup::onSwitchSource(CCObject*) {
    m_source = search::nextSource(m_source);
    s_lastSource = m_source;
    m_sourceSpr->setString(search::sourceName(m_source));
    if (!m_input->getString().empty()) {
        this->search();
    }
}

void SearchPopup::onOpenInBrowser(CCObject*) {
    std::string query = m_input->getString();
    if (query.empty()) {
        Notification::create("Type something to search first", NotificationIcon::Info)->show();
        return;
    }
    web::openLinkInBrowser(search::browserSearchURL(query));
}

void SearchPopup::search() {
    std::string query = m_input->getString();
    if (query.empty()) {
        this->setStatus("Type something and press search!");
        return;
    }
    s_lastQuery = query;
    m_results.clear();
    this->showResults();
    this->setStatus("", true);

    auto source = m_source;
    m_searchTask.spawn(
        "Editor Web Search",
        search::request(source, query),
        [this, source](web::WebResponse response) {
            auto res = search::parseResults(source, response);
            if (!res) {
                this->setStatus(res.unwrapErr());
                return;
            }
            m_results = std::move(res).unwrap();
            this->showResults();
            this->setStatus(m_results.empty() ? "No results found" : "");
        }
    );
}

void SearchPopup::setStatus(std::string const& text, bool loading) {
    m_status->setString(text.c_str());
    m_status->limitLabelWidth(LIST_WIDTH - 20.f, .45f, .1f);
    m_status->setVisible(!loading && !text.empty());
    m_spinner->setVisible(loading);
}

void SearchPopup::showResults() {
    auto content = m_list->m_contentLayer;
    content->removeAllChildren();

    std::vector<CCNode*> cells;
    float totalHeight = 0.f;
    for (size_t i = 0; i < m_results.size(); i++) {
        auto cell = this->createCell(m_results[i], static_cast<int>(i), LIST_WIDTH);
        totalHeight += cell->getContentHeight();
        cells.push_back(cell);
    }

    // Lay the cells out top to bottom
    float height = std::max(totalHeight, LIST_HEIGHT);
    content->setContentSize({ LIST_WIDTH, height });
    float y = height;
    for (auto cell : cells) {
        y -= cell->getContentHeight();
        cell->setPosition({ 0, y });
        content->addChild(cell);
    }

    m_list->scrollToTop();
    handleTouchPriority(this);
}

CCNode* SearchPopup::createCell(SearchResult const& result, int index, float width) {
    float textWidth = width - BUTTON_COLUMN_WIDTH - CELL_PADDING * 3;

    auto title = CCLabelBMFont::create(result.title.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0, 1 });
    title->limitLabelWidth(textWidth, .45f, .1f);

    SimpleTextArea* snippet = nullptr;
    float snippetHeight = 0.f;
    if (!result.snippet.empty()) {
        snippet = SimpleTextArea::create(result.snippet, "chatFont.fnt", .6f, textWidth);
        snippet->setMaxLines(4);
        snippet->setAnchorPoint({ 0, 1 });
        snippetHeight = snippet->getScaledContentHeight() + 3.f;
    }

    // Buttons, stacked vertically on the right
    auto menu = CCMenu::create();
    std::vector<CCMenuItemSpriteExtra*> buttons;
    if (result.pageID) {
        buttons.push_back(CCMenuItemSpriteExtra::create(
            smallButton("Read", "GJ_button_01.png"), this, menu_selector(SearchPopup::onRead)
        ));
    }
    if (!result.url.empty()) {
        buttons.push_back(CCMenuItemSpriteExtra::create(
            smallButton("Open", "GJ_button_02.png"), this, menu_selector(SearchPopup::onOpenResult)
        ));
        buttons.push_back(CCMenuItemSpriteExtra::create(
            smallButton("Copy", "GJ_button_05.png"), this, menu_selector(SearchPopup::onCopyResult)
        ));
    }
    constexpr float BUTTON_GAP = 21.f;
    float buttonsHeight = buttons.size() * BUTTON_GAP;

    float textHeight = title->getScaledContentHeight() + snippetHeight;
    float height = std::max(textHeight, buttonsHeight) + CELL_PADDING * 2;

    auto cell = CCNode::create();
    cell->setContentSize({ width, height });

    auto bg = CCLayerColor::create(index % 2 ? ccColor4B{ 0, 0, 0, 40 } : ccColor4B{ 255, 255, 255, 20 }, width, height);
    cell->addChild(bg);

    title->setPosition({ CELL_PADDING, height - CELL_PADDING });
    cell->addChild(title);

    if (snippet) {
        snippet->setPosition({ CELL_PADDING, height - CELL_PADDING - title->getScaledContentHeight() - 3.f });
        cell->addChild(snippet);
    }

    menu->setContentSize({ BUTTON_COLUMN_WIDTH, buttonsHeight });
    menu->ignoreAnchorPointForPosition(false);
    menu->setPosition({ width - CELL_PADDING - BUTTON_COLUMN_WIDTH / 2, height / 2 });
    for (size_t i = 0; i < buttons.size(); i++) {
        buttons[i]->setTag(index);
        buttons[i]->setPosition({ BUTTON_COLUMN_WIDTH / 2, buttonsHeight - BUTTON_GAP * (i + .5f) });
        menu->addChild(buttons[i]);
    }
    cell->addChild(menu);

    return cell;
}

SearchResult const* SearchPopup::resultFor(CCObject* sender) const {
    auto index = static_cast<CCNode*>(sender)->getTag();
    if (index < 0 || static_cast<size_t>(index) >= m_results.size()) return nullptr;
    return &m_results[index];
}

void SearchPopup::onOpenResult(CCObject* sender) {
    if (auto result = this->resultFor(sender)) {
        web::openLinkInBrowser(result->url);
    }
}

void SearchPopup::onCopyResult(CCObject* sender) {
    if (auto result = this->resultFor(sender)) {
        clipboard::write(result->url);
        Notification::create("Link copied to clipboard", NotificationIcon::Success)->show();
    }
}

void SearchPopup::onRead(CCObject* sender) {
    auto result = this->resultFor(sender);
    if (!result || !result->pageID) return;

    auto source = m_source;
    auto pageID = *result->pageID;
    this->setStatus("", true);

    m_articleTask.spawn(
        "Editor Web Search Article",
        search::requestArticle(source, pageID),
        [this, source, pageID](web::WebResponse response) {
            this->setStatus(m_results.empty() ? "No results found" : "");

            auto res = search::parseArticle(source, pageID, response);
            if (!res) {
                Notification::create(res.unwrapErr(), NotificationIcon::Error)->show();
                return;
            }
            auto article = std::move(res).unwrap();
            auto url = article.url;
            MDPopup::create(
                article.title.empty() ? "Article" : article.title,
                article.markdown,
                "Close", "Browser",
                [url](bool openBrowser) {
                    if (openBrowser) {
                        web::openLinkInBrowser(url);
                    }
                }
            )->show();
        }
    );
}
