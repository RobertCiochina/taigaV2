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

#include "image_provider.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariant>

#include "base/string.hpp"
#include "media/anime_db.hpp"
#include "taiga/network.hpp"
#include "taiga/path.hpp"

namespace gui {

void ImageProvider::fetchPoster(const int id) {
  const auto item = anime::db.item(id);

  if (!item || item->image_url.empty()) return;

  const auto url = QString::fromStdString(item->image_url);
  QNetworkRequest req{url};
  taiga::applyCommonHeaders(req);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  const auto reply = taiga::network()->get(req);

  connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
    const auto err = reply->error();
    const QVariant contentType = reply->header(QNetworkRequest::ContentTypeHeader);
    const QByteArray payload = reply->readAll();

    if (err != QNetworkReply::NoError || payload.isEmpty()) return;
    const QByteArray t = payload.trimmed();
    if (t.startsWith("<!DOCTYPE") || t.startsWith("<!doctype") || t.startsWith("<html") ||
        t.startsWith("<HTML")) {
      return;
    }

    QDir d(cacheDir());
    if (!d.exists() && !d.mkpath(QStringLiteral("."))) return;

    QString ext = QStringLiteral("img");
    if (contentType.isValid()) {
      const QString s = contentType.toString().toLower();
      if (s.contains(QStringLiteral("image/jpeg"))) ext = QStringLiteral("jpg");
      else if (s.contains(QStringLiteral("image/png"))) ext = QStringLiteral("png");
      else if (s.contains(QStringLiteral("image/webp"))) ext = QStringLiteral("webp");
    }
    if (ext == QStringLiteral("img")) {
      const auto item2 = anime::db.item(id);
      const QString path = item2 ? QUrl(QString::fromStdString(item2->image_url)).path() : QString{};
      const QString suffix = QFileInfo(path).suffix().toLower();
      if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg") ||
          suffix == QStringLiteral("png") || suffix == QStringLiteral("webp")) {
        ext = suffix == QStringLiteral("jpeg") ? QStringLiteral("jpg") : suffix;
      }
    }

    QFile file{fileNameWithExtension(id, ext)};
    if (!file.open(QIODevice::WriteOnly)) return;
    if (file.write(payload) <= 0) return;
    file.close();

    reloadPoster(id);
  });
}

const QPixmap* ImageProvider::loadPoster(const int id) {
  if (const auto it = m_pixmaps.find(id); it != m_pixmaps.end()) {
    return &it.value();
  }

  const QString cached = findCachedFileName(id);
  QImage image;
  if (!cached.isEmpty()) {
    QFile f(cached);
    if (f.open(QIODevice::ReadOnly)) {
      QImageReader reader(&f);
      reader.setAutoDetectImageFormat(true);
      image = reader.read();
    }
  }

  m_pixmaps[id] = !image.isNull() ? QPixmap::fromImage(image) : QPixmap{};

  if (image.isNull()) fetchPoster(id);

  return &m_pixmaps[id];
}

void ImageProvider::reloadPoster(const int id) {
  m_pixmaps.remove(id);
  loadPoster(id);
  emit posterChanged(id);
}

QString ImageProvider::cacheDir() const {
  const auto path = QString::fromStdString(taiga::get_data_path());
  return u"%1/v1/db/image"_s.arg(path);
}

QString ImageProvider::fileNameWithExtension(const int id, const QString& ext) const {
  return u"%1/%2.%3"_s.arg(cacheDir()).arg(id).arg(ext);
}

QString ImageProvider::findCachedFileName(const int id) const {
  static const QStringList exts{
      QStringLiteral("jpg"),
      QStringLiteral("png"),
      QStringLiteral("webp"),
      QStringLiteral("img"),
  };
  for (const QString& ext : exts) {
    const QString p = fileNameWithExtension(id, ext);
    if (QFile::exists(p)) return p;
  }
  return {};
}

}  // namespace gui
