/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "torrent_discovery.hpp"

#include <QDesktopServices>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include "taiga/settings.hpp"
#include "taiga/user_feedback.hpp"

namespace taiga {
namespace {

QString expandedTemplate(QString tmpl, const QString& title) {
  if (tmpl.trimmed().isEmpty()) tmpl = defaultTorrentDiscoverySearchUrl();
  tmpl.replace(QStringLiteral("%title%"), QString::fromUtf8(QUrl::toPercentEncoding(title)),
               Qt::CaseInsensitive);
  return tmpl;
}

QUrl urlFromExpandedString(const QString& expanded) {
  return QUrl::fromUserInput(expanded.trimmed());
}

QUrl maybeRewriteNyaaRssToHtml(QUrl u) {
  const QString host = u.host();
  if (!host.endsWith(QLatin1String("nyaa.si"), Qt::CaseInsensitive)) return u;

  QUrlQuery qy(u.query());
  if (qy.queryItemValue(QStringLiteral("page"), QUrl::FullyDecoded).compare(u"rss",
                                                                             Qt::CaseInsensitive) !=
      0) {
    return u;
  }

  QUrl out;
  out.setScheme(u.scheme().isEmpty() ? QStringLiteral("https") : u.scheme());
  out.setHost(host);
  out.setPath(QStringLiteral("/"));
  QUrlQuery outq;
  const QString f = qy.queryItemValue(QStringLiteral("f"), QUrl::FullyDecoded);
  const QString c = qy.queryItemValue(QStringLiteral("c"), QUrl::FullyDecoded);
  const QString qterm = qy.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded);
  if (!f.isEmpty()) outq.addQueryItem(QStringLiteral("f"), f);
  if (!c.isEmpty()) outq.addQueryItem(QStringLiteral("c"), c);
  if (!qterm.isEmpty()) outq.addQueryItem(QStringLiteral("q"), qterm);
  out.setQuery(outq);
  return out;
}

}  // namespace

QString defaultTorrentDiscoverySearchUrl() {
  return QStringLiteral("https://nyaa.si/?page=rss&c=1_2&f=0&q=%title%");
}

QString defaultTorrentDiscoveryFeedSourceUrl() {
  return QStringLiteral("https://www.tokyotosho.info/rss.php?filter=1,11&zwnj=0");
}

QUrl torrentDiscoveryFeedFetchUrl(const QString& template_with_placeholders, const QString& title) {
  return urlFromExpandedString(expandedTemplate(template_with_placeholders, title));
}

QUrl torrentDiscoveryCatalogFeedUrl(const QString& source_url_or_empty) {
  QString u = source_url_or_empty.trimmed();
  if (u.isEmpty()) u = defaultTorrentDiscoveryFeedSourceUrl();
  return QUrl::fromUserInput(u);
}

QUrl torrentDiscoveryResolvedUrl(const QString& template_with_placeholders, const QString& title) {
  return maybeRewriteNyaaRssToHtml(urlFromExpandedString(
      expandedTemplate(template_with_placeholders, title)));
}

bool openTorrentDiscoverySearch(const QString& title) {
  const QString t = title.trimmed();
  if (t.isEmpty()) {
    userFeedback(QStringLiteral("No title to search."), true);
    return false;
  }
  const QString raw = QString::fromStdString(settings.torrentDiscoverySearchUrl());
  const QUrl url = torrentDiscoveryResolvedUrl(raw, t);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || (scheme != u"http" && scheme != u"https")) {
    userFeedback(QStringLiteral("Invalid torrent search URL in settings."), true);
    return false;
  }
  if (!QDesktopServices::openUrl(url)) {
    userFeedback(QStringLiteral("Could not open the torrent search URL."), true);
    return false;
  }
  return true;
}

}  // namespace taiga
