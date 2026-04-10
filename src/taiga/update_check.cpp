/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "update_check.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/network.hpp"
#include "taiga/settings.hpp"
#include "taiga/version.hpp"

#include <semaver.hpp>

namespace taiga {

namespace {

QPointer<QNetworkReply> g_update_reply;
quint64 g_update_seq = 0;
qint64 g_last_silent_update_attempt_ms = 0;
constexpr qint64 kSilentUpdateCheckMinIntervalMs = 60LL * 60LL * 1000LL;

struct ParsedUpdate {
  bool parse_ok = false;
  bool has_newer = false;
  semaver::Version latest{};
  QString link;
};

ParsedUpdate parseUpdateRss(const QByteArray& body) {
  ParsedUpdate out;
  const semaver::Version current = version();
  semaver::Version best = current;
  QString best_link;

  QXmlStreamReader xml(body);
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement() || xml.name() != QLatin1String("item")) continue;

    QString guid;
    QString link;
    while (!xml.atEnd()) {
      xml.readNext();
      if (xml.isEndElement() && xml.name() == QLatin1String("item")) break;
      if (!xml.isStartElement()) continue;
      if (xml.name() == QLatin1String("guid")) {
        guid = xml.readElementText();
      } else if (xml.name() == QLatin1String("link")) {
        link = xml.readElementText();
      } else {
        xml.skipCurrentElement();
      }
    }

    if (guid.isEmpty()) continue;
    try {
      const semaver::Version iv(guid.toStdString());
      if (iv > best) {
        best = iv;
        best_link = link;
      }
    } catch (...) {
    }
  }

  if (xml.hasError()) return out;

  out.parse_ok = true;
  if (best > current) {
    out.has_newer = true;
    out.latest = best;
    out.link = best_link;
  }
  return out;
}

}  // namespace

void checkForUpdates(QWidget* parent_context, const bool silent) {
  if (!parent_context) return;
  QPointer<QWidget> guard(parent_context);

  if (silent) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (g_last_silent_update_attempt_ms > 0 &&
        now - g_last_silent_update_attempt_ms < kSilentUpdateCheckMinIntervalMs) {
      return;
    }
    if (g_update_reply) return;
    g_last_silent_update_attempt_ms = now;
  } else if (g_update_reply) {
    ++g_update_seq;
    g_update_reply->abort();
  }

  const quint64 my_seq = ++g_update_seq;

  QUrl url(QStringLiteral("https://taiga.moe/update.php"));
  QUrlQuery q;
  const std::string pre = version().prerelease;
  q.addQueryItem(QStringLiteral("channel"),
                 pre.empty() ? QStringLiteral("stable") : QString::fromStdString(pre));
  q.addQueryItem(QStringLiteral("check"), silent ? QStringLiteral("auto") : QStringLiteral("manual"));
  q.addQueryItem(QStringLiteral("version"), QString::fromStdString(version().to_string()));
  q.addQueryItem(QStringLiteral("service"),
                 QString::fromStdString(taiga::settings.service()));
  q.addQueryItem(QStringLiteral("username"),
                 QString::fromStdString(taiga::accounts.serviceUsername(taiga::settings.service())));
  url.setQuery(q);

  QNetworkRequest req(url);
  applyCommonHeaders(req);

  QNetworkReply* reply = taiga::network()->get(req);
  g_update_reply = reply;
  QObject::connect(reply, &QNetworkReply::finished, parent_context, [reply, guard, silent, my_seq]() {
    reply->deleteLater();
    if (my_seq != g_update_seq) return;
    g_update_reply.clear();
    if (!guard) return;

    if (reply->error() != QNetworkReply::NoError) {
      if (!silent) {
        QMessageBox::warning(guard, QObject::tr("Taiga"),
                             QObject::tr("Could not check for updates: %1").arg(reply->errorString()));
      }
      return;
    }

    const ParsedUpdate parsed = parseUpdateRss(reply->readAll());
    if (!parsed.parse_ok) {
      if (!silent) {
        QMessageBox::warning(guard, QObject::tr("Taiga"),
                             QObject::tr("Could not read update information from the server."));
      }
      return;
    }

    if (!parsed.has_newer) {
      if (!silent) {
        QMessageBox::information(guard, QObject::tr("Taiga"), QObject::tr("You are up to date."));
      }
      return;
    }

    const QString msg =
        QObject::tr("A newer version (%1) is available.\n\nOpen the download page?")
            .arg(QString::fromStdString(parsed.latest.to_string()));
    if (QMessageBox::question(guard, QObject::tr("Taiga"), msg,
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
      const QUrl open =
          parsed.link.isEmpty() ? QUrl(QStringLiteral("https://taiga.moe")) : QUrl(parsed.link);
      QDesktopServices::openUrl(open);
    }
  });
}

}  // namespace taiga
