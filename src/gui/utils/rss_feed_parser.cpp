/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "rss_feed_parser.hpp"

#include <QRegularExpression>
#include <QXmlStreamReader>

namespace gui {

namespace {

bool tagEq(const QStringView n, const QStringView s) {
  if (n.compare(s, Qt::CaseInsensitive) == 0) return true;
  const qsizetype i = n.lastIndexOf(u':');
  if (i < 0) return false;
  return n.sliced(i + 1).compare(s, Qt::CaseInsensitive) == 0;
}

void skipCurrentElement(QXmlStreamReader& xml) {
  if (!xml.isStartElement()) return;
  int depth = 1;
  while (!xml.atEnd() && depth > 0) {
    switch (xml.readNext()) {
      case QXmlStreamReader::StartElement:
        ++depth;
        break;
      case QXmlStreamReader::EndElement:
        --depth;
        break;
      default:
        break;
    }
  }
}

rss::Item parseRssItem(QXmlStreamReader& xml) {
  rss::Item item;
  while (xml.readNextStartElement()) {
    const QStringView n = xml.name();
    if (tagEq(n, u"title")) {
      item.title = xml.readElementText().toStdString();
    } else if (tagEq(n, u"link")) {
      item.link = xml.readElementText().toStdString();
    } else if (tagEq(n, u"pubDate")) {
      item.pub_date = xml.readElementText().toStdString();
    } else if (tagEq(n, u"description")) {
      item.description = xml.readElementText().toStdString();
    } else if (tagEq(n, u"guid")) {
      item.guid.value = xml.readElementText().toStdString();
    } else if (tagEq(n, u"enclosure")) {
      item.enclosure.url = xml.attributes().value(u"url").toString().toStdString();
      item.enclosure.type = xml.attributes().value(u"type").toString().toStdString();
      item.enclosure.length = xml.attributes().value(u"length").toString().toStdString();
      xml.skipCurrentElement();
    } else {
      xml.skipCurrentElement();
    }
  }
  return item;
}

std::optional<rss::Feed> parseRss2Feed(const QByteArray& xml_utf8, QString* error_message,
                                       const int max_items) {
  QXmlStreamReader xml(xml_utf8);
  if (xml.hasError()) {
    if (error_message) *error_message = xml.errorString();
    return std::nullopt;
  }

  rss::Feed feed;
  bool seen_rss = false;
  while (!xml.atEnd()) {
    if (xml.readNext() != QXmlStreamReader::StartElement) continue;
    if (tagEq(xml.name(), u"rss")) {
      seen_rss = true;
      break;
    }
    skipCurrentElement(xml);
  }

  if (!seen_rss) {
    if (error_message) *error_message = QStringLiteral("Not an RSS 2.0 document.");
    return std::nullopt;
  }

  if (!xml.readNextStartElement() || !tagEq(xml.name(), u"channel")) {
    if (error_message) *error_message = QStringLiteral("Missing RSS 2.0 channel.");
    return std::nullopt;
  }

  while (xml.readNextStartElement()) {
    const QStringView n = xml.name();
    if (tagEq(n, u"title")) {
      feed.channel.title = xml.readElementText().toStdString();
    } else if (tagEq(n, u"link")) {
      feed.channel.link = xml.readElementText().toStdString();
    } else if (tagEq(n, u"description")) {
      feed.channel.description = xml.readElementText().toStdString();
    } else if (tagEq(n, u"item")) {
      if (static_cast<int>(feed.items.size()) < max_items) {
        feed.items.push_back(parseRssItem(xml));
      } else {
        skipCurrentElement(xml);
      }
    } else {
      xml.skipCurrentElement();
    }
  }

  if (xml.hasError()) {
    if (error_message) *error_message = xml.errorString();
    return std::nullopt;
  }

  return feed;
}

std::optional<rss::Feed> parseRdfRss1Feed(const QByteArray& xml_utf8, QString* error_message,
                                           const int max_items) {
  QXmlStreamReader xml(xml_utf8);
  rss::Feed feed;

  while (!xml.atEnd()) {
    if (xml.readNext() != QXmlStreamReader::StartElement) continue;
    const QStringView n = xml.name();
    if (tagEq(n, u"RDF")) {
      continue;
    }
    if (tagEq(n, u"channel")) {
      while (xml.readNextStartElement()) {
        const QStringView cn = xml.name();
        if (tagEq(cn, u"title")) {
          feed.channel.title = xml.readElementText().toStdString();
        } else if (tagEq(cn, u"link")) {
          feed.channel.link = xml.readElementText().toStdString();
        } else if (tagEq(cn, u"description")) {
          feed.channel.description = xml.readElementText().toStdString();
        } else if (tagEq(cn, u"items")) {
          skipCurrentElement(xml);
        } else {
          xml.skipCurrentElement();
        }
      }
    } else if (tagEq(n, u"item")) {
      if (static_cast<int>(feed.items.size()) < max_items) {
        feed.items.push_back(parseRssItem(xml));
      } else {
        skipCurrentElement(xml);
      }
    } else {
      skipCurrentElement(xml);
    }
  }

  if (xml.hasError()) {
    if (error_message) *error_message = xml.errorString();
    return std::nullopt;
  }
  if (feed.items.empty()) {
    if (error_message) *error_message = QStringLiteral("No RSS 1.0 / RDF items found.");
    return std::nullopt;
  }
  return feed;
}

rss::Item parseAtomEntry(QXmlStreamReader& xml) {
  rss::Item item;
  while (xml.readNextStartElement()) {
    const QStringView n = xml.name();
    if (tagEq(n, u"title")) {
      item.title = xml.readElementText().toStdString();
    } else if (tagEq(n, u"link")) {
      const QString href = xml.attributes().value(u"href").toString();
      const QString rel = xml.attributes().value(u"rel").toString();
      xml.skipCurrentElement();
      if (href.isEmpty()) continue;
      if (rel.compare(u"enclosure", Qt::CaseInsensitive) == 0) {
        if (item.enclosure.url.empty()) item.enclosure.url = href.toStdString();
      } else if (rel.isEmpty() || rel.compare(u"alternate", Qt::CaseInsensitive) == 0) {
        if (item.link.empty()) item.link = href.toStdString();
      }
    } else if (tagEq(n, u"updated")) {
      item.pub_date = xml.readElementText().toStdString();
    } else if (tagEq(n, u"id")) {
      if (item.guid.value.empty()) item.guid.value = xml.readElementText().toStdString();
    } else if (tagEq(n, u"content") || tagEq(n, u"summary")) {
      if (item.description.empty()) item.description = xml.readElementText().toStdString();
    } else {
      xml.skipCurrentElement();
    }
  }
  return item;
}

std::optional<rss::Feed> parseAtomFeed(const QByteArray& xml_utf8, QString* error_message,
                                       const int max_items) {
  QXmlStreamReader xml(xml_utf8);
  rss::Feed feed;

  while (!xml.atEnd()) {
    if (xml.readNext() != QXmlStreamReader::StartElement) continue;
    if (!tagEq(xml.name(), u"feed")) {
      skipCurrentElement(xml);
      continue;
    }
    while (xml.readNextStartElement()) {
      const QStringView n = xml.name();
      if (tagEq(n, u"title")) {
        feed.channel.title = xml.readElementText().toStdString();
      } else if (tagEq(n, u"link")) {
        const QString href = xml.attributes().value(u"href").toString();
        xml.skipCurrentElement();
        if (!href.isEmpty() && feed.channel.link.empty()) feed.channel.link = href.toStdString();
      } else if (tagEq(n, u"entry")) {
        if (static_cast<int>(feed.items.size()) < max_items) {
          feed.items.push_back(parseAtomEntry(xml));
        } else {
          skipCurrentElement(xml);
        }
      } else {
        xml.skipCurrentElement();
      }
    }
    break;
  }

  if (xml.hasError()) {
    if (error_message) *error_message = xml.errorString();
    return std::nullopt;
  }
  if (feed.items.empty()) {
    if (error_message) *error_message = QStringLiteral("Not an Atom feed (no entries).");
    return std::nullopt;
  }
  return feed;
}

void normalizeTorrentFeed(rss::Feed& feed) {
  static const QRegularExpression magnetHref(
      QStringLiteral("href\\s*=\\s*\"(magnet:[^\"]+)\""), QRegularExpression::CaseInsensitiveOption);

  for (auto& it : feed.items) {
    const QString desc = QString::fromStdString(it.description);
    const auto mm = magnetHref.match(desc);
    if (mm.hasMatch()) {
      it.namespace_elements[kTorrentFeedMagnetKey] = mm.captured(1).toStdString();
    }

    const QString link = QString::fromStdString(it.link);
    const QString guid = QString::fromStdString(it.guid.value);
    const bool link_is_torrent = link.endsWith(u".torrent", Qt::CaseInsensitive) ||
                                 link.startsWith(u"magnet:", Qt::CaseInsensitive);

    if (link_is_torrent) {
      if (it.enclosure.url.empty()) {
        it.enclosure.url = it.link;
      }
      if (guid.startsWith(u"http://", Qt::CaseInsensitive) ||
          guid.startsWith(u"https://", Qt::CaseInsensitive)) {
        it.link = it.guid.value;
      }
    }
  }
}

}  // namespace

std::optional<rss::Feed> parseSyndicationFeed(const QByteArray& xml_utf8, QString* error_message,
                                              const int max_items) {
  QString e2, e3, e4;
  if (auto f = parseRss2Feed(xml_utf8, &e2, max_items)) {
    normalizeTorrentFeed(*f);
    return f;
  }
  if (auto f = parseRdfRss1Feed(xml_utf8, &e3, max_items)) {
    normalizeTorrentFeed(*f);
    return f;
  }
  if (auto f = parseAtomFeed(xml_utf8, &e4, max_items)) {
    normalizeTorrentFeed(*f);
    return f;
  }
  if (error_message) {
    *error_message = QStringLiteral("RSS 2.0: %1\nRSS 1.0/RDF: %2\nAtom: %3").arg(e2, e3, e4);
  }
  return std::nullopt;
}

}  // namespace gui
