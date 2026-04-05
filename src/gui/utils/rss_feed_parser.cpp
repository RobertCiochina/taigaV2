/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "rss_feed_parser.hpp"

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

rss::Item parseItem(QXmlStreamReader& xml) {
  rss::Item item;
  while (xml.readNextStartElement()) {
    const QStringView n = xml.name();
    if (tagEq(n, u"title")) {
      item.title = xml.readElementText().toStdString();
    } else if (tagEq(n, u"link")) {
      item.link = xml.readElementText().toStdString();
    } else if (tagEq(n, u"pubDate")) {
      item.pub_date = xml.readElementText().toStdString();
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

}  // namespace

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
    if (error_message) *error_message = QStringLiteral("Missing RSS channel.");
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
        feed.items.push_back(parseItem(xml));
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

}  // namespace gui
