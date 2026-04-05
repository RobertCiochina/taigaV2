/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "settings.hpp"

#include <algorithm>

#include <QStringView>
#include <QXmlStreamReader>
#include <chrono>

#include "base/log.hpp"
#include "base/xml.hpp"
#include "media/anime.hpp"
#include "taiga/accounts.hpp"
#include "taiga/settings.hpp"

#define XML_ATTR(name) xml.attributes().value(name)
#define XML_ATTR_INT(name) XML_ATTR(name).toInt()
#define XML_ATTR_STR(name) XML_ATTR(name).toString().toStdString()

namespace compat::v1 {

void parseAccountElement(QXmlStreamReader&, const taiga::Settings&, const taiga::Accounts&);
void parseAnimeElement(QXmlStreamReader&, const taiga::Settings&);
void parseProgramElement(QXmlStreamReader&, const taiga::Settings&);
void parseRecognitionElement(QXmlStreamReader&, const taiga::Settings&);

void readSettings(const std::string& path, const taiga::Settings& settings,
                  const taiga::Accounts& accounts) {
  base::XmlFileReader xml;

  if (!xml.open(QString::fromStdString(path))) {
    LOGE("{}", xml.file().errorString().toStdString());
    return;
  }

  if (!xml.readElement(u"settings")) {
    xml.raiseError("Invalid settings file.");
  }

  while (xml.readNextStartElement()) {
    if (xml.name() == u"account") {
      parseAccountElement(xml, settings, accounts);
    } else if (xml.name() == u"anime") {
      parseAnimeElement(xml, settings);
    } else if (xml.name() == u"program") {
      parseProgramElement(xml, settings);
    } else if (xml.name() == u"recognition") {
      parseRecognitionElement(xml, settings);
    } else {
      // @TODO: announce, rss
      xml.skipCurrentElement();
    }
  }

  if (xml.hasError()) {
    LOGE("{}", xml.errorString().toStdString());
  }
}

void parseAccountElement(QXmlStreamReader& xml, const taiga::Settings& settings,
                         const taiga::Accounts& accounts) {
  while (xml.readNextStartElement()) {
    if (xml.name() == u"update") {
      settings.setService(XML_ATTR_STR(u"activeservice"));
      xml.skipCurrentElement();

    } else if (xml.name() == u"anilist") {
      accounts.setAnilistUsername(XML_ATTR_STR(u"username"));
      accounts.setAnilistToken(XML_ATTR_STR(u"token"));
      xml.skipCurrentElement();

    } else if (xml.name() == u"kitsu") {
      accounts.setKitsuEmail(XML_ATTR_STR(u"email"));
      accounts.setKitsuUsername(XML_ATTR_STR(u"username"));
      accounts.setKitsuPassword(XML_ATTR_STR(u"password"));
      xml.skipCurrentElement();

    } else if (xml.name() == u"myanimelist") {
      accounts.setMyanimelistUsername(XML_ATTR_STR(u"username"));
      accounts.setMyanimelistAccessToken(XML_ATTR_STR(u"accesstoken"));
      accounts.setMyanimelistRefreshToken(XML_ATTR_STR(u"refreshtoken"));
      xml.skipCurrentElement();

    } else {
      xml.skipCurrentElement();
    }
  }
}

void parseAnimeElement(QXmlStreamReader& xml, const taiga::Settings& settings) {
  std::vector<std::string> libraryFolders;

  while (xml.readNextStartElement()) {
    if (xml.name() == u"folders") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"root") {
          libraryFolders.push_back(XML_ATTR_STR(u"folder"));
          xml.skipCurrentElement();
        } else {
          xml.skipCurrentElement();
        }
      }

    } else {
      xml.skipCurrentElement();
    }
  }

  settings.setLibraryFolders(libraryFolders);
}

namespace {

bool xmlAttrBool(const QStringView value, const bool fallback) {
  if (value.isEmpty()) return fallback;
  return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || value == u"1";
}

}  // namespace

void parseProgramElement(QXmlStreamReader& xml, const taiga::Settings& settings) {
  while (xml.readNextStartElement()) {
    if (xml.name() == u"proxy") {
      settings.setProxyHost(XML_ATTR_STR(u"host"));
      settings.setProxyUsername(XML_ATTR_STR(u"username"));
      settings.setProxyPassword(XML_ATTR_STR(u"password"));
      xml.skipCurrentElement();
    } else if (xml.name() == u"general") {
      const auto attrs = xml.attributes();
      const auto er = attrs.value(u"enablerecognition");
      if (!er.isEmpty()) {
        settings.setMediaDetectionEnabled(xmlAttrBool(er, true));
      }
      const auto es = attrs.value(u"enablesharing");
      if (!es.isEmpty()) {
        settings.setSharingEnabled(xmlAttrBool(es, true));
      }
      const auto ey = attrs.value(u"enablesync");
      if (!ey.isEmpty()) {
        settings.setListSynchronizationEnabled(xmlAttrBool(ey, true));
      }
      const auto close_tray = attrs.value(u"close");
      if (!close_tray.isEmpty()) {
        settings.setCloseToTray(xmlAttrBool(close_tray, false));
      }
      const auto min_tray = attrs.value(u"minimize");
      if (!min_tray.isEmpty()) {
        settings.setMinimizeToTray(xmlAttrBool(min_tray, false));
      }
      const auto hide_sb = attrs.value(u"hidesidebar");
      if (!hide_sb.isEmpty()) {
        settings.setNavigationSidebarVisible(!xmlAttrBool(hide_sb, false));
      }
      xml.skipCurrentElement();
    } else if (xml.name() == u"startup") {
      const auto attrs = xml.attributes();
      const auto cv = attrs.value(u"checkversion");
      if (!cv.isEmpty()) {
        settings.setCheckForUpdatesOnStartup(xmlAttrBool(cv, true));
      }
      const auto ce = attrs.value(u"checkeps");
      if (!ce.isEmpty()) {
        settings.setScanLibraryOnStartup(xmlAttrBool(ce, false));
      }
      const auto start_min = attrs.value(u"minimize");
      if (!start_min.isEmpty()) {
        settings.setStartMinimized(xmlAttrBool(start_min, false));
      }
      xml.skipCurrentElement();
    } else if (xml.name() == u"list") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"action") {
          const auto attrs = xml.attributes();
          const auto tl = attrs.value(u"titlelang");
          if (!tl.isEmpty()) {
            const QString s = tl.toString();
            if (s.compare(u"english", Qt::CaseInsensitive) == 0) {
              settings.setListTitleLanguage(anime::TitleLanguage::English);
            } else if (s.compare(u"native", Qt::CaseInsensitive) == 0) {
              settings.setListTitleLanguage(anime::TitleLanguage::Native);
            } else {
              settings.setListTitleLanguage(anime::TitleLanguage::Romaji);
            }
          } else {
            const auto et = attrs.value(u"englishtitles");
            if (!et.isEmpty() && xmlAttrBool(et, false)) {
              settings.setListTitleLanguage(anime::TitleLanguage::English);
            }
          }
          {
            const auto dc = attrs.value(u"doubleclick");
            if (!dc.isEmpty()) {
              bool ok = false;
              const int v = dc.toInt(&ok);
              if (ok) {
                settings.setListDoubleClickAction(
                    static_cast<taiga::ListRowAction>(std::clamp(v, 0, 5)));
              }
            }
            const auto mc = attrs.value(u"middleclick");
            if (!mc.isEmpty()) {
              bool ok = false;
              const int v = mc.toInt(&ok);
              if (ok) {
                settings.setListMiddleClickAction(
                    static_cast<taiga::ListRowAction>(std::clamp(v, 0, 5)));
              }
            }
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"progress") {
          const auto attrs = xml.attributes();
          const auto sa = attrs.value(u"showaired");
          if (!sa.isEmpty()) {
            settings.setListProgressShowAired(xmlAttrBool(sa, true));
          }
          const auto sv = attrs.value(u"showavailable");
          if (!sv.isEmpty()) {
            settings.setListProgressShowAvailable(xmlAttrBool(sv, true));
          }
          xml.skipCurrentElement();
        } else {
          xml.skipCurrentElement();
        }
      }
    } else {
      xml.skipCurrentElement();
    }
  }
}

void parseRecognitionElement(QXmlStreamReader& xml, const taiga::Settings& settings) {
  while (xml.readNextStartElement()) {
    if (xml.name() == u"general") {
      const auto seconds = std::chrono::seconds{XML_ATTR_INT(u"detectioninterval")};
      settings.setMediaDetectionInterval(seconds);
      xml.skipCurrentElement();

    } else {
      xml.skipCurrentElement();
    }
  }
}

}  // namespace compat::v1
