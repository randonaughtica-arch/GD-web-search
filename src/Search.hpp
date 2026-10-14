#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

enum class SearchSource {
    Wikipedia,
    GDWiki,
    DuckDuckGo,
};

struct SearchResult {
    std::string title;
    std::string snippet;
    std::string url;
    // Set for wiki results, which can be read inside the game
    std::optional<int64_t> pageID;
};

struct Article {
    std::string title;
    std::string markdown;
    std::string url;
};

namespace search {
    SearchSource sourceFromString(std::string_view name);
    char const* sourceName(SearchSource source);
    SearchSource nextSource(SearchSource source);

    geode::utils::web::WebFuture request(SearchSource source, std::string const& query);
    geode::Result<std::vector<SearchResult>> parseResults(
        SearchSource source, geode::utils::web::WebResponse const& response
    );

    geode::utils::web::WebFuture requestArticle(SearchSource source, int64_t pageID);
    geode::Result<Article> parseArticle(
        SearchSource source, int64_t pageID, geode::utils::web::WebResponse const& response
    );

    std::string browserSearchURL(std::string const& query);
    std::string pageURL(SearchSource source, int64_t pageID);

    std::string stripHtml(std::string_view html);
    std::string describeError(geode::utils::web::WebResponse const& response);
}
