/**
 * Taiga
 * Copyright (C) 2010-2025, Eren Okka
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

#include "play.hpp"

#include <QDesktopServices>
#include <QUrl>
#include <random>

#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/scanner.hpp"

namespace track {

bool playEpisode(int animeId, int number) {
  const auto libraryFolders = taiga::settings.libraryFolders();

  for (const auto& folder : libraryFolders) {
    const auto episodePath = findEpisode(QString::fromStdString(folder), animeId, number);
    if (episodePath) {
      qDebug() << "Found file:" << *episodePath;
      return QDesktopServices::openUrl(QUrl::fromLocalFile(*episodePath));
    }
  }

  return false;
}

bool playNextEpisode(int animeId) {
  const auto item = anime::db.item(animeId);

  if (!item) return false;

  const auto entry = anime::db.entry(animeId);

  const int total_episodes = item->episode_count;
  const int last_episode = entry ? std::min(entry->watched_episodes, total_episodes) : 0;
  const int next_episode = last_episode + 1;

  return playEpisode(animeId, next_episode);
}

bool playRandomFromListing() {
  const auto& entries = anime::db.entries();
  if (entries.empty()) return false;

  thread_local std::mt19937 gen{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, static_cast<int>(entries.size()) - 1);
  return playNextEpisode(entries[static_cast<size_t>(dist(gen))].anime_id);
}

}  // namespace track
