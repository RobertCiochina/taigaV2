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
#include <optional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringView>
#include <QXmlStreamReader>
#include <chrono>

#include "base/log.hpp"
#include "base/xml.hpp"
#include "gui/models/anime_list_model.hpp"
#include "media/anime.hpp"
#include "taiga/accounts.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"

#define XML_ATTR(name) xml.attributes().value(name)
#define XML_ATTR_INT(name) XML_ATTR(name).toInt()
#define XML_ATTR_STR(name) XML_ATTR(name).toString().toStdString()

namespace {

bool xmlAttrBool(const QStringView value, const bool fallback) {
  if (value.isEmpty()) return fallback;
  return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || value == u"1";
}

bool jsonToBool(const QJsonValue& v) {
  if (v.isBool()) return v.toBool();
  if (v.isDouble()) return v.toInt() != 0;
  if (v.isString()) {
    const QString s = v.toString();
    return s.compare(u"true", Qt::CaseInsensitive) == 0 || s == u"1";
  }
  return false;
}

}  // namespace

namespace compat::v1 {

void parseAccountElement(QXmlStreamReader&, const taiga::Settings&, const taiga::Accounts&);
void parseAnimeElement(QXmlStreamReader&, const taiga::Settings&);
void parseProgramElement(QXmlStreamReader&, const taiga::Settings&);
void parseRecognitionElement(QXmlStreamReader&, const taiga::Settings&);
void parseRssElement(QXmlStreamReader&, const taiga::Settings&);
void parseAnnounceElement(QXmlStreamReader&, const taiga::Settings&);

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
    } else if (xml.name() == u"rss") {
      parseRssElement(xml, settings);
    } else if (xml.name() == u"announce") {
      parseAnnounceElement(xml, settings);
    } else {
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
      const auto delay_attr = XML_ATTR(u"delay");
      if (!delay_attr.isEmpty()) {
        bool ok = false;
        const int d = delay_attr.toInt(&ok);
        if (ok && d >= 0) {
          settings.setSyncListUpdateDelaySeconds(d);
        }
      }
      const auto ask = XML_ATTR(u"asktoconfirm");
      if (!ask.isEmpty()) {
        settings.setSyncListPushAskConfirm(xmlAttrBool(ask, true));
      }
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
        } else if (xml.name() == u"scan") {
          const int min_sz = XML_ATTR_INT(u"minfilesize");
          if (min_sz > 0) {
            settings.setLibraryScanMinFileSizeBytes(min_sz);
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"watch") {
          const auto we = xml.attributes().value(u"enabled");
          if (!we.isEmpty()) {
            settings.setLibraryWatchFoldersEnabled(xmlAttrBool(we, true));
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

  settings.setLibraryFolders(libraryFolders);
}

namespace {

/// Maps v1 `program/list/sort/column` string (attribute `column` on `<sort>`) to Qt list column index.
std::optional<int> v1ListSortColumnToQt(const QString& col) {
  const QStringView c = QStringView{col}.trimmed();
  if (c.isEmpty()) return {};
  if (c.compare(u"anime_title", Qt::CaseInsensitive) == 0) return gui::AnimeListModel::COLUMN_TITLE;
  if (c.compare(u"user_progress", Qt::CaseInsensitive) == 0) return gui::AnimeListModel::COLUMN_PROGRESS;
  if (c.compare(u"user_rating", Qt::CaseInsensitive) == 0) return gui::AnimeListModel::COLUMN_SCORE;
  if (c.compare(u"anime_average_rating", Qt::CaseInsensitive) == 0)
    return gui::AnimeListModel::COLUMN_AVERAGE;
  if (c.compare(u"anime_type", Qt::CaseInsensitive) == 0) return gui::AnimeListModel::COLUMN_TYPE;
  if (c.compare(u"anime_season", Qt::CaseInsensitive) == 0) return gui::AnimeListModel::COLUMN_SEASON;
  if (c.compare(u"user_date_started", Qt::CaseInsensitive) == 0)
    return gui::AnimeListModel::COLUMN_STARTED;
  if (c.compare(u"user_date_completed", Qt::CaseInsensitive) == 0)
    return gui::AnimeListModel::COLUMN_COMPLETED;
  if (c.compare(u"user_last_updated", Qt::CaseInsensitive) == 0)
    return gui::AnimeListModel::COLUMN_LAST_UPDATED;
  if (c.compare(u"user_notes", Qt::CaseInsensitive) == 0) return gui::AnimeListModel::COLUMN_NOTES;
  if (c.compare(u"anime_status", Qt::CaseInsensitive) == 0)
    return gui::AnimeListModel::COLUMN_TITLE;
  return {};
}

/// Like `v1ListSortColumnToQt` but skips v1-only columns with no Qt analogue (e.g. `anime_status`).
std::optional<int> v1ListLayoutColumnToQt(const QString& col) {
  const QStringView c = QStringView{col}.trimmed();
  if (c.isEmpty()) return {};
  if (c.compare(u"anime_status", Qt::CaseInsensitive) == 0) return {};
  return v1ListSortColumnToQt(col);
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
      const auto ssl_nr = attrs.value(u"sslnorevoke");
      if (!ssl_nr.isEmpty()) {
        settings.setNetworkRelaxedTls(xmlAttrBool(ssl_nr, false));
      }
      const auto autostart = attrs.value(u"autostart");
      if (!autostart.isEmpty()) {
        settings.setStartWithWindows(xmlAttrBool(autostart, false));
      }
      xml.skipCurrentElement();
    } else if (xml.name() == u"notifications") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"balloon") {
          const auto battrs = xml.attributes();
          const auto rec = battrs.value(u"recognized");
          if (!rec.isEmpty()) {
            settings.setMediaNotifyRecognizedBalloon(xmlAttrBool(rec, true));
          }
          const auto nrec = battrs.value(u"notrecognized");
          if (!nrec.isEmpty()) {
            settings.setMediaNotifyUnrecognizedBalloon(xmlAttrBool(nrec, true));
          }
          xml.skipCurrentElement();
        } else {
          xml.skipCurrentElement();
        }
      }
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
        } else if (xml.name() == u"sort") {
          // Taiga v1 stores these as attributes on <program><list><sort/> (see settings DeserializeFromXml).
          const auto attrs = xml.attributes();
          const auto col = attrs.value(u"column");
          if (!col.isEmpty()) {
            if (const auto mapped = v1ListSortColumnToQt(col.toString())) {
              taiga::session.setAnimeListSortColumn(*mapped);
            }
          }
          const auto ord = attrs.value(u"order");
          if (!ord.isEmpty()) {
            bool ok = false;
            const int o = ord.toInt(&ok);
            if (ok) {
              taiga::session.setAnimeListSortOrder(o < 0 ? Qt::DescendingOrder : Qt::AscendingOrder);
            }
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"filter") {
          while (xml.readNextStartElement()) {
            if (xml.name() == u"episodes") {
              const auto attrs = xml.attributes();
              const auto hi = attrs.value(u"highlight");
              if (!hi.isEmpty()) {
                settings.setListHighlightNextEpisodeOnDisk(xmlAttrBool(hi, true));
              }
              const auto top = attrs.value(u"highlightedontop");
              if (!top.isEmpty()) {
                settings.setListHighlightAvailableOnTop(xmlAttrBool(top, false));
              }
              xml.skipCurrentElement();
            } else {
              xml.skipCurrentElement();
            }
          }
        } else if (xml.name() == u"columns") {
          QJsonArray column_layout;
          while (xml.readNextStartElement()) {
            if (xml.name() == u"column") {
              const auto attrs = xml.attributes();
              const QString name = attrs.value(u"name").toString();
              if (const auto col = v1ListLayoutColumnToQt(name)) {
                column_layout.append(QJsonObject{
                    {QStringLiteral("c"), *col},
                    {QStringLiteral("w"), attrs.value(u"width").toInt()},
                    {QStringLiteral("v"), xmlAttrBool(attrs.value(u"visible"), true)},
                });
              }
              xml.skipCurrentElement();
            } else {
              xml.skipCurrentElement();
            }
          }
          if (!column_layout.isEmpty()) {
            taiga::session.setPendingV1ListColumnLayout(
                QString::fromUtf8(QJsonDocument(column_layout).toJson(QJsonDocument::Compact)));
          }
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
      const auto attrs = xml.attributes();
      const auto di = attrs.value(u"detectioninterval");
      if (!di.isEmpty()) {
        bool ok = false;
        const int sec = di.toInt(&ok);
        if (ok && sec > 0) {
          settings.setMediaDetectionInterval(std::chrono::milliseconds(sec * 1000LL));
        }
      }
      const auto lp = attrs.value(u"lookup_parent_directories");
      if (!lp.isEmpty()) {
        settings.setLibraryScanLookupParentDirectories(xmlAttrBool(lp, true));
      }
      xml.skipCurrentElement();

    } else if (xml.name() == u"mediaplayers") {
      const auto mp_attrs = xml.attributes();
      const auto mpen = mp_attrs.value(u"enabled");
      if (!mpen.isEmpty()) {
        settings.setMediaDetectionPlayersEnabled(xmlAttrBool(mpen, true));
      }
      const QString launch = mp_attrs.value(u"launchpath").toString();
      if (!launch.isEmpty()) {
        settings.setMediaPlayerExecutablePath(launch.toStdString());
      }
      xml.skipCurrentElement();
    } else if (xml.name() == u"streaming") {
      const auto en = xml.attributes().value(u"enabled");
      if (!en.isEmpty()) {
        settings.setMediaDetectionStreamingEnabled(xmlAttrBool(en, false));
      }
      xml.skipCurrentElement();
    } else if (xml.name() == u"anitomy") {
      const auto ig = xml.attributes().value(u"ignored_strings");
      if (!ig.isEmpty()) {
        settings.setRecognitionIgnoredSubstrings(ig.toString().toStdString());
      }
      xml.skipCurrentElement();
    } else {
      xml.skipCurrentElement();
    }
  }
}

void parseRssElement(QXmlStreamReader& xml, const taiga::Settings& settings) {
  while (xml.readNextStartElement()) {
    if (xml.name() == u"torrent") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"search") {
          const QString addr = xml.attributes().value(u"address").toString();
          if (!addr.isEmpty()) {
            settings.setTorrentDiscoverySearchUrl(addr.toStdString());
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"source") {
          const QString addr = xml.attributes().value(u"address").toString();
          if (!addr.isEmpty()) {
            settings.setTorrentDiscoveryFeedSourceUrl(addr.toStdString());
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"options") {
          const auto attrs = xml.attributes();
          const auto ac = attrs.value(u"autocheck");
          if (!ac.isEmpty()) {
            settings.setTorrentDiscoveryAutoCheckEnabled(xmlAttrBool(ac, true));
          }
          const auto ci = attrs.value(u"checkinterval");
          if (!ci.isEmpty()) {
            bool ok = false;
            const int v = ci.toInt(&ok);
            if (ok && v > 0) {
              settings.setTorrentDiscoveryAutoCheckIntervalMinutes(v);
            }
          }
          const auto dm = attrs.value(u"downloadusemagnet");
          if (!dm.isEmpty()) {
            settings.setTorrentDownloadUseMagnet(xmlAttrBool(dm, false));
          }
          const QString dlp = attrs.value(u"downloadpath").toString();
          if (!dlp.isEmpty()) {
            settings.setTorrentClientDownloadPath(dlp.toStdString());
          }
          const QString tfp = attrs.value(u"filedownloadpath").toString();
          if (!tfp.isEmpty()) {
            settings.setTorrentFileSavePath(tfp.toStdString());
          }
          const auto af = attrs.value(u"autosetfolder");
          if (!af.isEmpty()) {
            settings.setTorrentDownloadUseAnimeFolder(xmlAttrBool(af, true));
          }
          const auto uf = attrs.value(u"autousefolder");
          if (!uf.isEmpty()) {
            settings.setTorrentDownloadFallbackOnClientPath(xmlAttrBool(uf, false));
          }
          const auto cf = attrs.value(u"autocreatefolder");
          if (!cf.isEmpty()) {
            settings.setTorrentDownloadCreateSubfolder(xmlAttrBool(cf, false));
          }
          const auto na = attrs.value(u"newaction");
          if (!na.isEmpty()) {
            bool ok = false;
            const int a = na.toInt(&ok);
            if (ok && a == 2) {
              settings.setTorrentDiscoveryNewCatalogAction(taiga::TorrentDiscoveryNewCatalogAction::Download);
            } else if (ok) {
              settings.setTorrentDiscoveryNewCatalogAction(taiga::TorrentDiscoveryNewCatalogAction::Notify);
            }
          }
          const QString dsb = attrs.value(u"downloadsortby").toString();
          if (!dsb.isEmpty()) {
            settings.setTorrentRssSortBy(dsb.toStdString());
          }
          const QString dso = attrs.value(u"downloadsortorder").toString();
          if (!dso.isEmpty()) {
            settings.setTorrentRssSortOrder(dso.toStdString());
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"filter") {
          const auto attrs = xml.attributes();
          const auto fe = attrs.value(u"enabled");
          if (!fe.isEmpty()) {
            settings.setTorrentFeedFilterEnabled(xmlAttrBool(fe, true));
          }
          const auto am = attrs.value(u"archive_maxcount");
          if (!am.isEmpty()) {
            bool ok = false;
            const int v = am.toInt(&ok);
            if (ok && v > 0) {
              settings.setTorrentFeedArchiveMaxItems(v);
            }
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"application") {
          const auto attrs = xml.attributes();
          const auto op = attrs.value(u"open");
          if (!op.isEmpty()) {
            settings.setTorrentAppOpen(xmlAttrBool(op, true));
          }
          const auto mo = attrs.value(u"mode");
          if (!mo.isEmpty()) {
            bool ok = false;
            const int m = mo.toInt(&ok);
            if (ok) {
              settings.setTorrentAppMode(m == 2 ? 2 : 1);
            }
          }
          const QString ap = attrs.value(u"path").toString();
          if (!ap.isEmpty()) {
            settings.setTorrentAppExecutablePath(ap.toStdString());
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

void parseAnnounceElement(QXmlStreamReader& xml, const taiga::Settings& settings) {
  QJsonObject root;
  while (xml.readNextStartElement()) {
    const QString tag = xml.name().toString();
    QJsonObject section;
    const auto attrs = xml.attributes();
    for (const QXmlStreamAttribute& a : attrs) {
      const QString n = a.name().toString();
      const QString v = a.value().toString();
      if (v.compare(u"true", Qt::CaseInsensitive) == 0 || v == u"1") {
        section[n] = true;
      } else if (v.compare(u"false", Qt::CaseInsensitive) == 0 || v == u"0") {
        section[n] = false;
      } else {
        bool ok = false;
        const qint64 i = v.toLongLong(&ok);
        if (ok) {
          section[n] = i;
        } else {
          section[n] = v;
        }
      }
    }
    root[tag] = section;
    xml.skipCurrentElement();
  }
  if (!root.isEmpty()) {
    settings.setAnnounceV1MigrationJson(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)).toStdString());
  }

  const QJsonObject http = root.value(QStringLiteral("http")).toObject();
  if (!http.isEmpty()) {
    if (http.contains(QStringLiteral("enabled"))) {
      settings.setAnnounceHttpEnabled(jsonToBool(http.value(QStringLiteral("enabled"))));
    }
    QString url = http.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) url = http.value(QStringLiteral("address")).toString();
    if (!url.isEmpty()) {
      settings.setAnnounceHttpUrl(url.toStdString());
    }
    QString fmt = http.value(QStringLiteral("format")).toString();
    if (fmt.isEmpty()) fmt = http.value(QStringLiteral("body")).toString();
    if (!fmt.isEmpty()) {
      settings.setAnnounceHttpBodyFormat(fmt.toStdString());
    }
  }
}

}  // namespace compat::v1
