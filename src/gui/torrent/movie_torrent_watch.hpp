/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

class QTimer;

#include "base/rss.hpp"

class QNetworkReply;

namespace gui {

struct MovieTorrentMatch {
  int count = 0;
  QString bestTitle;
  QString bestLink;
  bool bestLinkCanAdd = false;
};

/// Accept a Nyaa-style result when it overlaps one of `titles` and looks like a movie
/// (no episode number, or an explicit movie token). Series episodes and season batches are
/// rejected. Include regexes, when non-empty, must match; any exclude regex match rejects.
MovieTorrentMatch matchMovieTorrentFeed(const rss::Feed& feed, const QStringList& titles,
                                        const QList<QRegularExpression>& include,
                                        const QList<QRegularExpression>& exclude);

struct MovieTorrentSnapshot {
  bool onDisk = false;
  bool watching = false;
  bool checking = false;
  int hitCount = 0;
  qint64 lastCheckSecs = 0;
  QString bestTitle;
};

/// Slow background Nyaa check for Watching movies that are not finished and not on disk.
class MovieTorrentWatch final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(MovieTorrentWatch)

public:
  static MovieTorrentWatch* instance();

  void start();
  void checkNow(int animeId);
  void noteAutoAdded(int animeId, const QString& link);
  MovieTorrentSnapshot snapshot(int animeId) const;

signals:
  void updated(int animeId);
  /// A new magnet or .torrent link is ready and auto-add is enabled.
  void addRequested(int animeId, QString link, QString folderName);

private:
  explicit MovieTorrentWatch(QObject* parent = nullptr);

  struct State {
    qint64 nextDue = 0;
    qint64 lastCheck = 0;
    int missStreak = 0;
    int hitCount = 0;
    QString bestTitle;
    QString bestLink;
    QString addedLink;
  };

  void armEligible();
  void pump();
  void beginFetch(int animeId, const QString& query, bool tryingFallback);
  void finishFetch(int animeId, const MovieTorrentMatch& match, bool hadTransportError);
  void save() const;
  void load();
  bool isPollTarget(int animeId) const;

  QHash<int, State> m_state;
  QNetworkReply* m_reply = nullptr;
  QTimer* m_timer = nullptr;
  int m_activeId = 0;
  bool m_tryingFallback = false;
  qint64 m_nextRequestNotBefore = 0;
  bool m_started = false;
};

}  // namespace gui
