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

#include "network.hpp"

#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "base/string.hpp"
#include "taiga/application.hpp"
#include "taiga/config.h"
#include "taiga/settings.hpp"

namespace taiga {

NetworkAccessManager::NetworkAccessManager(QObject* parent) : QNetworkAccessManager{parent} {
  setAutoDeleteReplies(true);
  setTransferTimeout(std::chrono::seconds{10});

  connect(this, &QNetworkAccessManager::finished, this, [](QNetworkReply* reply) {
    if (!app()->isDebug()) return;
    qDebug() << "Response status:"
             << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "Response headers:";
    for (const auto& [name, value] : reply->rawHeaderPairs()) {
      qDebug().noquote() << name << ": " << value;
    }
  });
}

QHttpHeaders NetworkAccessManager::commonHeaders() {
  QHttpHeaders headers;

  static const auto userAgentString = []() {
    return u"%1/%2.%3"_s.arg(TAIGA_APP_NAME).arg(TAIGA_VERSION_MAJOR).arg(TAIGA_VERSION_MINOR);
  };
  headers.append(QHttpHeaders::WellKnownHeader::UserAgent, userAgentString());

  return headers;
}

void NetworkAccessManager::applyProxyFromSettings() {
  const auto host = QString::fromStdString(taiga::settings.proxyHost()).trimmed();
  if (host.isEmpty()) {
    setProxy(QNetworkProxy{QNetworkProxy::NoProxy});
    return;
  }

  QString hostName = host;
  int port = 8080;
  if (const int colon = hostName.lastIndexOf(':'); colon > 0) {
    bool ok = false;
    const int p = hostName.mid(colon + 1).toInt(&ok);
    if (ok && p > 0 && p <= 65535) {
      port = p;
      hostName = hostName.first(colon);
    }
  }

  QNetworkProxy proxy{QNetworkProxy::HttpProxy, hostName, static_cast<quint16>(port)};
  proxy.setUser(QString::fromStdString(taiga::settings.proxyUsername()));
  proxy.setPassword(QString::fromStdString(taiga::settings.proxyPassword()));
  setProxy(proxy);
}

void applyCommonHeaders(QNetworkRequest& request) {
  const QHttpHeaders headers = NetworkAccessManager::commonHeaders();
  for (qsizetype i = 0; i < headers.size(); ++i) {
    const QLatin1StringView name = headers.nameAt(i);
    request.setRawHeader(QByteArray(name.data(), static_cast<int>(name.size())),
                         QByteArray(headers.valueAt(i)));
  }
}

}  // namespace taiga
