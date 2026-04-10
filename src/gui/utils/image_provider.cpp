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
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRunnable>
#include <QThreadPool>
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
  if (m_network_fetch_pending.contains(id)) return;
  m_network_fetch_pending.insert(id);

  const auto url = QString::fromStdString(item->image_url);
  QNetworkRequest req{url};
  taiga::applyCommonHeaders(req);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  const auto reply = taiga::network()->get(req);

  connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
    m_network_fetch_pending.remove(id);
    const auto err = reply->error();
    const QVariant contentType = reply->header(QNetworkRequest::ContentTypeHeader);
    const QByteArray payload = reply->readAll();

    const auto finishReply = [reply]() { reply->deleteLater(); };

    if (err != QNetworkReply::NoError || payload.isEmpty()) {
      finishReply();
      return;
    }
    const QByteArray t = payload.trimmed();
    if (t.startsWith("<!DOCTYPE") || t.startsWith("<!doctype") || t.startsWith("<html") ||
        t.startsWith("<HTML")) {
      finishReply();
      return;
    }

    QDir d(cacheDir());
    if (!d.exists() && !d.mkpath(QStringLiteral("."))) {
      finishReply();
      return;
    }

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
    if (!file.open(QIODevice::WriteOnly)) {
      finishReply();
      return;
    }
    if (file.write(payload) <= 0) {
      finishReply();
      return;
    }
    file.close();

    reloadPoster(id);
    finishReply();
  });
}

const QPixmap* ImageProvider::loadPoster(const int id) {
  if (const auto it = m_pixmaps.find(id); it != m_pixmaps.end()) {
    return &it.value();
  }

  const QString cached = findCachedFileName(id);
  if (cached.isEmpty()) {
    // Nothing on disk yet — return empty and trigger async fetch.
    m_pixmaps[id] = QPixmap{};
    fetchPoster(id);
    return &m_pixmaps[id];
  }

  // Cached on disk. Decoding can be expensive when a large result set is first shown (e.g. Search
  // Reset filters expanding to "all"). Decode in the background and update the view when ready.
  m_pixmaps[id] = QPixmap{};  // placeholder
  if (!m_loading.contains(id)) {
    m_loading.insert(id);
    const quint64 gen = m_generation;

    struct DecodeJob final : public QRunnable {
      QPointer<ImageProvider> provider;
      int id = 0;
      QString path;
      quint64 gen = 0;
      void run() override {
        QImage image;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
          QImageReader reader(&f);
          reader.setAutoDetectImageFormat(true);
          image = reader.read();
        }
        const QPixmap pix = !image.isNull() ? QPixmap::fromImage(image) : QPixmap{};
        if (!provider) return;
        QMetaObject::invokeMethod(
            provider.data(),
            [p = provider, id = id, pix = pix, gen = gen]() {
              if (!p) return;
              // If cache was cleared/reloaded since the job started, ignore.
              if (p->m_generation != gen) return;
              p->m_loading.remove(id);
              if (!pix.isNull()) {
                p->m_pixmaps[id] = pix;
              } else {
                // If decode failed, fall back to network fetch.
                p->m_pixmaps[id] = QPixmap{};
                p->fetchPoster(id);
              }
              emit p->posterChanged(id);
            },
            Qt::QueuedConnection);
      }
    };

    auto* job = new DecodeJob();
    job->setAutoDelete(true);
    job->provider = this;
    job->id = id;
    job->path = cached;
    job->gen = gen;
    QThreadPool::globalInstance()->start(job);
  }

  return &m_pixmaps[id];
}

void ImageProvider::reloadPoster(const int id) {
  m_pixmaps.remove(id);
  m_loading.remove(id);
  loadPoster(id);
  emit posterChanged(id);
}

void ImageProvider::clearPosterCache() {
  m_pixmaps.clear();
  m_loading.clear();
  m_network_fetch_pending.clear();
  ++m_generation;
  QDir dir(cacheDir());
  if (!dir.exists()) return;
  for (const auto& info : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
    dir.remove(info.fileName());
  }
}

qint64 ImageProvider::posterCacheSize(int* fileCount) const {
  QDir dir(cacheDir());
  if (!dir.exists()) {
    if (fileCount) *fileCount = 0;
    return 0;
  }
  qint64 total = 0;
  int count = 0;
  for (const auto& info : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
    total += info.size();
    ++count;
  }
  if (fileCount) *fileCount = count;
  return total;
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
