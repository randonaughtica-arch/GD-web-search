#include "Search.hpp"

#include <cctype>
#include <charconv>

using namespace geode::prelude;

namespace {
    constexpr auto USER_AGENT = "EditorWebSearch/1.0 (Geode mod for Geometry Dash)";
    constexpr auto GD_WIKI = "https://geometry-dash.fandom.com";
    constexpr size_t MAX_ARTICLE_LENGTH = 20000;

    std::string wikiBase(SearchSource source) {
        if (source == SearchSource::GDWiki) {
            return GD_WIKI;
        }
        auto lang = Mod::get()->getSettingValue<std::string>("wiki-language");
        if (lang.empty()) lang = "en";
        return fmt::format("https://{}.wikipedia.org", lang);
    }

    std::string apiURL(SearchSource source) {
        // Wikipedia serves its API from /w/, Fandom from the root
        return wikiBase(source) + (source == SearchSource::GDWiki ? "/api.php" : "/w/api.php");
    }

    web::WebRequest baseRequest() {
        web::WebRequest req;
        req.userAgent(USER_AGENT);
        req.timeout(std::chrono::seconds(15));
        return req;
    }

    // Returns a null value instead of failing when the key is missing
    matjson::Value const& field(matjson::Value const& value, std::string_view key) {
        static matjson::Value const null;
        if (!value.isObject() || !value.contains(key)) return null;
        return value[key];
    }

    std::string str(matjson::Value const& value) {
        return value.asString().unwrapOr("");
    }

    std::vector<matjson::Value> arr(matjson::Value const& value) {
        if (!value.isArray()) return {};
        return value.asArray().unwrap();
    }

    std::string trim(std::string_view s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return std::string(s.substr(start, end - start + 1));
    }

    std::string urlEncode(std::string_view s) {
        std::string out;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            }
            else if (c == ' ') {
                out += '+';
            }
            else {
                out += fmt::format("%{:02X}", c);
            }
        }
        return out;
    }

    void appendUTF8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x110000) {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string decodeEntities(std::string_view s) {
        static std::unordered_map<std::string_view, std::string_view> const named = {
            {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
            {"nbsp", " "}, {"ndash", "-"}, {"mdash", "-"}, {"hellip", "..."},
            {"lsquo", "'"}, {"rsquo", "'"}, {"ldquo", "\""}, {"rdquo", "\""},
        };
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '&') {
                auto semi = s.find(';', i);
                if (semi != std::string_view::npos && semi - i <= 10) {
                    auto entity = s.substr(i + 1, semi - i - 1);
                    if (!entity.empty() && entity[0] == '#') {
                        uint32_t cp = 0;
                        bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                        auto digits = entity.substr(hex ? 2 : 1);
                        auto res = std::from_chars(digits.data(), digits.data() + digits.size(), cp, hex ? 16 : 10);
                        if (res.ec == std::errc() && res.ptr == digits.data() + digits.size()) {
                            appendUTF8(out, cp == 0xA0 ? ' ' : cp);
                            i = semi;
                            continue;
                        }
                    }
                    else if (auto it = named.find(entity); it != named.end()) {
                        out += it->second;
                        i = semi;
                        continue;
                    }
                }
            }
            out += s[i];
        }
        return out;
    }

    // Removes every <tag ...>...</tag> element (including nested ones of the same
    // tag) whose opening tag satisfies the predicate
    std::string removeElements(
        std::string_view html, std::string_view tag,
        std::function<bool(std::string_view)> const& predicate = nullptr
    ) {
        auto const open = fmt::format("<{}", tag);
        auto const close = fmt::format("</{}", tag);

        auto isTagAt = [&](size_t pos, std::string const& prefix) {
            if (html.substr(pos, prefix.size()) != prefix) return false;
            auto next = pos + prefix.size();
            return next < html.size() && (html[next] == '>' || html[next] == ' ' || html[next] == '/' || html[next] == '\n');
        };

        std::string out;
        out.reserve(html.size());
        size_t i = 0;
        while (i < html.size()) {
            if (isTagAt(i, open)) {
                auto tagEnd = html.find('>', i);
                if (tagEnd == std::string_view::npos) break;
                auto openTag = html.substr(i, tagEnd - i + 1);
                if (!predicate || predicate(openTag)) {
                    bool selfClosing = openTag.size() >= 2 && openTag[openTag.size() - 2] == '/';
                    size_t j = tagEnd + 1;
                    int depth = selfClosing ? 0 : 1;
                    while (depth > 0 && j < html.size()) {
                        if (isTagAt(j, open)) depth++;
                        else if (isTagAt(j, close)) depth--;
                        j++;
                    }
                    if (depth == 0 && !selfClosing) {
                        j = html.find('>', j);
                        j = j == std::string_view::npos ? html.size() : j + 1;
                    }
                    i = std::min(j, html.size());
                    continue;
                }
            }
            out += html[i];
            i++;
        }
        return out;
    }

    bool hasClass(std::string_view openTag, std::string_view cls) {
        auto pos = openTag.find("class=\"");
        if (pos == std::string_view::npos) return false;
        auto end = openTag.find('"', pos + 7);
        return openTag.substr(pos + 7, end - pos - 7).find(cls) != std::string_view::npos;
    }

    void replaceAll(std::string& s, std::string_view from, std::string_view to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    // Collapses runs of spaces and blank lines left over after stripping tags
    std::string tidyWhitespace(std::string_view s) {
        std::string out;
        int newlines = 0;
        bool pendingSpace = false;
        for (char c : s) {
            if (c == '\n') {
                newlines++;
                pendingSpace = false;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') {
                pendingSpace = true;
                continue;
            }
            if (newlines > 0) {
                if (!out.empty()) out += newlines > 1 ? "\n\n" : "\n";
                newlines = 0;
            }
            else if (pendingSpace && !out.empty()) {
                out += ' ';
            }
            pendingSpace = false;
            out += c;
        }
        return out;
    }

    // Escapes characters that would otherwise be interpreted by the markdown renderer
    std::string escapeMarkdown(std::string_view s) {
        std::string out;
        out.reserve(s.size());
        bool lineStart = true;
        for (char c : s) {
            if (std::string_view("\\*_`[]<>|").find(c) != std::string_view::npos || (lineStart && (c == '#' || c == '-' || c == '+'))) {
                out += '\\';
            }
            out += c;
            lineStart = c == '\n';
        }
        return out;
    }

    std::string truncateArticle(std::string text, std::string const& url) {
        if (text.size() <= MAX_ARTICLE_LENGTH) return text;
        auto cut = text.rfind("\n\n", MAX_ARTICLE_LENGTH);
        if (cut == std::string::npos || cut < MAX_ARTICLE_LENGTH / 2) cut = MAX_ARTICLE_LENGTH;
        // Don't cut a UTF-8 sequence in half
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) cut--;
        text.resize(cut);
        text += fmt::format("\n\n---\n\n*Article truncated. [Continue reading in your browser]({})*", url);
        return text;
    }

    // Wikipedia's plain text extracts mark sections with "== Heading =="
    std::string wikiExtractToMarkdown(std::string_view extract) {
        std::string out;
        size_t start = 0;
        while (start <= extract.size()) {
            auto end = extract.find('\n', start);
            if (end == std::string_view::npos) end = extract.size();
            auto line = trim(extract.substr(start, end - start));

            size_t level = 0;
            while (level < line.size() && line[level] == '=') level++;
            if (level >= 2 && line.size() > level * 2 && line.ends_with(std::string(level, '='))) {
                auto heading = trim(std::string_view(line).substr(level, line.size() - level * 2));
                out += fmt::format("\n\n{} {}\n\n", std::string(std::min<size_t>(level, 4), '#'), escapeMarkdown(heading));
            }
            else {
                // Each line of an extract is a paragraph
                out += escapeMarkdown(line);
                out += "\n\n";
            }
            start = end + 1;
        }
        return tidyWhitespace(out);
    }

    std::string fandomHtmlToMarkdown(std::string_view html) {
        std::string s(html);
        s = removeElements(s, "script");
        s = removeElements(s, "style");
        s = removeElements(s, "table");
        s = removeElements(s, "aside");
        s = removeElements(s, "figure");
        s = removeElements(s, "sup");
        s = removeElements(s, "span", [](auto tag) { return hasClass(tag, "mw-editsection"); });
        s = removeElements(s, "div", [](auto tag) {
            return hasClass(tag, "toc") || hasClass(tag, "gallery") || hasClass(tag, "navbox")
                || hasClass(tag, "thumb") || tag.find("id=\"toc\"") != std::string_view::npos;
        });

        // Turn block-level tags into line breaks / markers before stripping everything.
        // Markers use control characters so that escaping doesn't touch them.
        for (auto [tag, marker] : {
            std::pair{"h2", "\x01" "2"}, std::pair{"h3", "\x01" "3"}, std::pair{"h4", "\x01" "4"},
        }) {
            replaceAll(s, fmt::format("<{}", tag), fmt::format("\n\n{}<{}", marker, tag));
            replaceAll(s, fmt::format("</{}>", tag), "\n\n");
        }
        replaceAll(s, "<li>", "\n\x02<li>");
        replaceAll(s, "<li ", "\n\x02<li ");
        for (auto tag : {"<p>", "<p ", "</p>", "<br>", "<br/>", "<br />", "</ul>", "</ol>", "</div>", "<dd", "<dt"}) {
            replaceAll(s, tag, fmt::format("\n{}", tag));
        }

        auto text = escapeMarkdown(trim(search::stripHtml(s)));
        replaceAll(text, "\x01" "2", "## ");
        replaceAll(text, "\x01" "3", "### ");
        replaceAll(text, "\x01" "4", "#### ");
        replaceAll(text, "\x02", "- ");
        return tidyWhitespace(text);
    }

    void collectDuckDuckGoTopics(matjson::Value const& topics, std::vector<SearchResult>& out, std::string const& group) {
        for (auto const& topic : arr(topics)) {
            if (topic.contains("Topics")) {
                collectDuckDuckGoTopics(field(topic, "Topics"), out, str(field(topic, "Name")));
                continue;
            }
            auto text = str(field(topic, "Text"));
            auto url = str(field(topic, "FirstURL"));
            if (text.empty() || url.empty()) continue;

            // Topics look like "Title - description"
            SearchResult result;
            auto dash = text.find(" - ");
            if (dash != std::string::npos && dash < 80) {
                result.title = text.substr(0, dash);
                result.snippet = text.substr(dash + 3);
            }
            else {
                result.title = text.size() > 60 ? text.substr(0, 57) + "..." : text;
                result.snippet = text;
            }
            if (!group.empty()) {
                result.snippet = fmt::format("[{}] {}", group, result.snippet);
            }
            result.url = url;
            out.push_back(std::move(result));
        }
    }
}

SearchSource search::sourceFromString(std::string_view name) {
    if (name == "GD Wiki") return SearchSource::GDWiki;
    if (name == "DuckDuckGo") return SearchSource::DuckDuckGo;
    return SearchSource::Wikipedia;
}

char const* search::sourceName(SearchSource source) {
    switch (source) {
        case SearchSource::Wikipedia: return "Wikipedia";
        case SearchSource::GDWiki: return "GD Wiki";
        case SearchSource::DuckDuckGo: return "DuckDuckGo";
    }
    return "Wikipedia";
}

SearchSource search::nextSource(SearchSource source) {
    switch (source) {
        case SearchSource::Wikipedia: return SearchSource::GDWiki;
        case SearchSource::GDWiki: return SearchSource::DuckDuckGo;
        case SearchSource::DuckDuckGo: return SearchSource::Wikipedia;
    }
    return SearchSource::Wikipedia;
}

web::WebFuture search::request(SearchSource source, std::string const& query) {
    auto req = baseRequest();
    if (source == SearchSource::DuckDuckGo) {
        req.param("q", query);
        req.param("format", "json");
        req.param("no_html", "1");
        req.param("skip_disambig", "0");
        req.param("t", "geode-editor-web-search");
        return req.get("https://api.duckduckgo.com/");
    }

    req.param("action", "query");
    req.param("list", "search");
    req.param("srsearch", query);
    req.param("srlimit", Mod::get()->getSettingValue<int64_t>("result-count"));
    req.param("srprop", "snippet");
    req.param("format", "json");
    req.param("utf8", "1");
    return req.get(apiURL(source));
}

Result<std::vector<SearchResult>> search::parseResults(SearchSource source, web::WebResponse const& response) {
    if (!response.ok()) {
        return Err(describeError(response));
    }
    // DuckDuckGo sometimes answers with an empty body when it has nothing
    auto body = response.string().unwrapOr("");
    if (trim(body).empty()) {
        return Ok(std::vector<SearchResult>{});
    }
    GEODE_UNWRAP_INTO(auto json, matjson::parse(body).mapErr([](auto const& err) {
        return fmt::format("Invalid response: {}", err.message);
    }));

    std::vector<SearchResult> results;

    if (source == SearchSource::DuckDuckGo) {
        auto answer = str(field(json, "Answer"));
        if (!answer.empty()) {
            results.push_back({ "Answer", decodeEntities(answer), str(field(json, "AbstractURL")), std::nullopt });
        }
        auto abstract = str(field(json, "AbstractText"));
        if (!abstract.empty()) {
            auto heading = str(field(json, "Heading"));
            results.push_back({
                heading.empty() ? "Summary" : heading,
                fmt::format("{} ({})", abstract, str(field(json, "AbstractSource"))),
                str(field(json, "AbstractURL")),
                std::nullopt,
            });
        }
        auto definition = str(field(json, "Definition"));
        if (!definition.empty()) {
            results.push_back({ "Definition", definition, str(field(json, "DefinitionURL")), std::nullopt });
        }
        collectDuckDuckGoTopics(field(json, "Results"), results, "");
        collectDuckDuckGoTopics(field(json, "RelatedTopics"), results, "");
        return Ok(std::move(results));
    }

    if (json.contains("error")) {
        return Err(str(field(field(json, "error"), "info")));
    }
    for (auto const& item : arr(field(field(json, "query"), "search"))) {
        auto pageID = field(item, "pageid").asInt();
        if (!pageID) continue;
        results.push_back({
            str(field(item, "title")),
            stripHtml(str(field(item, "snippet"))),
            pageURL(source, pageID.unwrap()),
            pageID.unwrap(),
        });
    }
    return Ok(std::move(results));
}

web::WebFuture search::requestArticle(SearchSource source, int64_t pageID) {
    auto req = baseRequest();
    req.param("format", "json");
    req.param("formatversion", "2");

    if (source == SearchSource::Wikipedia) {
        req.param("action", "query");
        req.param("pageids", pageID);
        req.param("prop", "extracts");
        req.param("explaintext", "1");
        req.param("exsectionformat", "wiki");
    }
    else {
        // Fandom doesn't have TextExtracts, so we parse the rendered HTML instead
        req.param("action", "parse");
        req.param("pageid", pageID);
        req.param("prop", "text");
        req.param("disablelimitreport", "1");
        req.param("disableeditsection", "1");
    }
    return req.get(apiURL(source));
}

Result<Article> search::parseArticle(SearchSource source, int64_t pageID, web::WebResponse const& response) {
    if (!response.ok()) {
        return Err(describeError(response));
    }
    GEODE_UNWRAP_INTO(auto json, response.json());

    Article article;
    article.url = pageURL(source, pageID);

    if (source == SearchSource::Wikipedia) {
        auto pages = arr(field(field(json, "query"), "pages"));
        if (pages.empty()) {
            return Err(std::string("Article not found"));
        }
        article.title = str(field(pages[0], "title"));
        article.markdown = wikiExtractToMarkdown(str(field(pages[0], "extract")));
    }
    else {
        auto const& parse = field(json, "parse");
        if (parse.isNull()) {
            return Err(str(field(field(json, "error"), "info")));
        }
        article.title = str(field(parse, "title"));
        article.markdown = fandomHtmlToMarkdown(str(field(parse, "text")));
    }

    if (trim(article.markdown).empty()) {
        article.markdown = fmt::format("*This page has no readable text.* [Open it in your browser]({})", article.url);
    }
    article.markdown = truncateArticle(std::move(article.markdown), article.url);
    return Ok(std::move(article));
}

std::string search::browserSearchURL(std::string const& query) {
    auto engine = Mod::get()->getSettingValue<std::string>("browser-engine");
    auto q = urlEncode(query);
    if (engine == "DuckDuckGo") return "https://duckduckgo.com/?q=" + q;
    if (engine == "Bing") return "https://www.bing.com/search?q=" + q;
    return "https://www.google.com/search?q=" + q;
}

std::string search::pageURL(SearchSource source, int64_t pageID) {
    auto base = wikiBase(source);
    return source == SearchSource::GDWiki
        ? fmt::format("{}/index.php?curid={}", base, pageID)
        : fmt::format("{}/w/index.php?curid={}", base, pageID);
}

std::string search::stripHtml(std::string_view html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>' && inTag) inTag = false;
        else if (!inTag) out += c;
    }
    return decodeEntities(out);
}

std::string search::describeError(web::WebResponse const& response) {
    std::string msg(response.errorMessage());
    if (msg.empty()) {
        msg = response.string().unwrapOr("");
    }
    if (msg.size() > 120) {
        msg = msg.substr(0, 117) + "...";
    }
    if (response.code() > 0) {
        return fmt::format("Request failed (HTTP {}): {}", response.code(), msg);
    }
    return fmt::format("Request failed: {}", msg.empty() ? "no connection" : msg);
}
