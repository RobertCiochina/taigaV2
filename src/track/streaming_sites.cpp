/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "streaming_sites.hpp"

#include <QUrl>

#include <array>
#include <regex>
#include <set>

namespace track::streaming {

namespace {

struct Rule {
  std::string_view slug;
  const char* label;
  std::regex url_pattern;
  std::regex title_pattern;
};

// Slugs align with stored provider keys and `settings_keys.cpp`.
// URL/title regexes are ported from the legacy implementation where applicable; Crunchyroll,
// Funimation, and HiDive use host-based patterns.
const std::array<Rule, 17> kRules{{
    {"animelab",
     "AnimeLab",
     std::regex(R"(animelab\.com/player/)"),
     std::regex(R"(AnimeLab - (.+))")},
    {"adn",
     "Anime Digital Network",
     std::regex(R"(animedigitalnetwork\.fr/video/[^/]+/[0-9]+)"),
     std::regex(R"((.+) - streaming -.* ADN)")},
    {"ann",
     "Anime News Network",
     std::regex(R"(animenewsnetwork\.(?:com|cc)/video/[0-9]+)"),
     std::regex(R"((.+) - Anime News Network)")},
    {"bilibili",
     "Bilibili",
     std::regex(R"(bilibili\.tv/[^/]+/play/[0-9]+)"),
     std::regex(R"((.+) - Bilibili)")},
    {"crunchyroll",
     "Crunchyroll",
     std::regex(R"(crunchyroll\.com)"),
     std::regex(R"((.+) - .* - Crunchyroll)")},
    {"funimation",
     "Funimation",
     std::regex(R"(funimation\.com)"),
     std::regex(R"((.+) - Funimation)")},
    {"hidive",
     "HiDive",
     std::regex(R"(hidive\.com)"),
     std::regex(R"((.+) - HiDive)")},
    {"jellyfin",
     "Jellyfin",
     std::regex(R"(.+/web/(?:index\.html)?#!/video)"),
     std::regex(R"(Jellyfin|(.+))")},
    {"plex",
     "Plex",
     std::regex(R"(^app\.plex\.tv/desktop|^[^/]*?plex\.tv/web/|^localhost:32400/web/|^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:32400/web/|^plex\.[a-z0-9-]+\.[a-z0-9-]+|^[^/]*[a-z0-9-]+\.[a-z0-9-]+/plex)"),
     // U+25B6 (play) in UTF-8 — avoid u8"" so MSVC builds std::regex<char> cleanly.
     std::regex(R"(Plex|(?:\xE2\x96\xB6 )?(.+))")},
    {"rokuchannel",
     "Roku Channel",
     std::regex(R"(therokuchannel\.roku\.com/watch/.+)"),
     std::regex(R"(Watch (.+) Online for Free \| The Roku Channel \| Roku)")},
    {"tubi",
     "Tubi",
     std::regex(R"(tubitv\.com/tv-shows/.+)"),
     std::regex(R"(Watch (.+) - Free TV Shows \| Tubi)")},
    {"veoh",
     "Veoh",
     std::regex(R"(veoh\.com/watch/)"),
     std::regex(R"(Watch Videos Online \| (.+) \| Veoh\.com)")},
    {"viz",
     "VIZ",
     std::regex(R"(viz\.com/watch/streaming/[^/]+-(?:episode-[0-9]+|movie)/)"),
     std::regex(R"((.+) // VIZ)")},
    {"vrv",
     "VRV",
     std::regex(R"(vrv\.co/watch/)"),
     std::regex(R"((.+) - Watch on VRV)")},
    {"wakanim",
     "Wakanim",
     std::regex(R"(wakanim\.tv/[^/]+/v2/catalogue/episode/[^/]+/)"),
     std::regex(R"((.+) (?:auf|on|sur) Wakanim\.TV.*)")},
    {"yahoo",
     "Yahoo View",
     std::regex(R"(view\.yahoo\.com/show/[^/]+/episode/[^/]+/)"),
     std::regex(R"(Watch .+ Free Online - (.+) \| Yahoo View)")},
    {"youtube",
     "YouTube",
     std::regex(R"(youtube\.com/watch)"),
     std::regex(R"(YouTube|(?:\xE2\x96\xB6 )?(.+) - YouTube)")},
}};

bool applyTitleRegex(std::string& title, const std::regex& pattern) {
  std::smatch match;
  if (!std::regex_match(title, match, pattern)) return false;
  for (size_t i = 1; i < match.size(); ++i) {
    if (!match.str(i).empty()) {
      title = match.str(i);
      return true;
    }
  }
  if (!match.empty()) {
    title.clear();
    return true;
  }
  return false;
}

void cleanSiteSpecific(std::string_view slug, std::string& title) {
  if (slug == "adn") {
    for (;;) {
      const auto pos = title.find(" : ");
      if (pos == std::string::npos) break;
      title.replace(pos, 3, " - ");
    }
  } else if (slug == "ann") {
    static const std::regex pattern{R"( \((?:s|d)(?:, uncut)?\))"};
    title = std::regex_replace(title, pattern, "");
  } else if (slug == "rokuchannel" || slug == "tubi") {
    static const std::regex pattern{R"( S(\d+):E(\d+) )"};
    title = std::regex_replace(title, pattern, " S$1E$2 ");
  } else if (slug == "vrv") {
    for (size_t i = 0; i + 4 < title.size(); ++i) {
      if (title.compare(i, 5, ": EP ") == 0) {
        title.replace(i, 5, " - EP ");
        break;
      }
    }
  } else if (slug == "wakanim") {
    static const std::regex pattern{
        R"((?:Episode (\d+)|Film|Movie) - (?:ENGDUB - )?(.+))"};
    std::smatch match;
    if (std::regex_match(title, match, pattern)) {
      title = match.str(2) + (match.length(1) ? " - Episode " + match.str(1) : "");
    }
  }
}

}  // namespace

const std::vector<ProviderUiEntry>& providerUiEntries() {
  static const std::vector<ProviderUiEntry> k = [] {
    std::vector<ProviderUiEntry> v;
    v.reserve(kRules.size());
    for (const auto& r : kRules) {
      v.push_back(ProviderUiEntry{r.slug, r.label});
    }
    return v;
  }();
  return k;
}

std::optional<std::string_view> matchProviderSlugByUrl(const std::string_view url) {
  if (url.empty()) return std::nullopt;
  std::string s{url};
  // v1 strips scheme for matching
  static const std::regex lead_http{R"(^https?://)"};
  s = std::regex_replace(s, lead_http, "");

  for (const auto& rule : kRules) {
    if (std::regex_search(s, rule.url_pattern)) {
      return rule.slug;
    }
  }
  return std::nullopt;
}

void normalizeBrowserTitle(const std::string_view url, std::string& title) {
  const QString qurl = QString::fromUtf8(url.data(), static_cast<int>(url.size()));
  QUrl parsed{qurl.startsWith(QStringLiteral("http://")) || qurl.startsWith(QStringLiteral("https://"))
                    ? qurl
                    : QStringLiteral("http://") + qurl};
  QString title_q = QString::fromStdString(title);
  if (!parsed.host().isEmpty() && title_q.startsWith(parsed.host(), Qt::CaseInsensitive)) {
    title.clear();
    return;
  }
  if (title_q.startsWith(QStringLiteral("http://")) || title_q.startsWith(QStringLiteral("https://"))) {
    title.clear();
    return;
  }

  static const std::set<std::string> common_titles{
      "Blank Page",       "InPrivate",          "New Tab",
      "Private Browsing", "Private browsing",   "Problem loading page",
      "Speed Dial",       "Untitled",
  };
  if (common_titles.count(title) > 0) {
    title.clear();
    return;
  }

  static const std::vector<std::string> suffixes{
      " - Crashed",
      " - Network error",
  };
  for (const auto& suf : suffixes) {
    if (title.size() >= suf.size() &&
        title.compare(title.size() - suf.size(), suf.size(), suf) == 0) {
      title.clear();
      return;
    }
  }

  static const std::vector<std::string> audio_suffixes{" - Audio muted", " - Audio playing"};
  for (const auto& suf : audio_suffixes) {
    if (title.size() >= suf.size() &&
        title.compare(title.size() - suf.size(), suf.size(), suf) == 0) {
      title.resize(title.size() - suf.size());
    }
  }
}

bool refineTitleForProvider(const std::string_view slug, std::string& title_utf8) {
  for (const auto& rule : kRules) {
    if (rule.slug != slug) continue;
    if (!applyTitleRegex(title_utf8, rule.title_pattern)) {
      return false;
    }
    cleanSiteSpecific(slug, title_utf8);
    return true;
  }
  return false;
}

}  // namespace track::streaming
