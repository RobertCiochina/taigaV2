#pragma once

#include <functional>

#include <QString>

#include <QObject>

#include "media/anime_list.hpp"

namespace sync::kitsu {

using ListFetchComplete = std::function<void(bool ok, QString message)>;

class Service final : public QObject {
  Q_OBJECT
public:
  explicit Service(QObject* parent = nullptr);

  static Service* instance();

  void fetchListEntries(ListFetchComplete on_complete = {});

  void fetchAnime(int id);
  void saveListEntry(const ListEntry& entry);
  void deleteListEntry(int anime_id);

private:
  void authenticate(ListFetchComplete then, bool continue_with_library = true);
  void fetchUserId(ListFetchComplete then, bool continue_with_library = true);
  void ensureSession(ListFetchComplete ready);
  void fetchLibraryPage(int offset, int total, ListFetchComplete done);

  QString token_;
  QString user_id_;
};

}  // namespace sync::kitsu
