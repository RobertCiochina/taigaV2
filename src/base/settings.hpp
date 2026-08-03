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

#pragma once

#include <QSettings>
#include <QString>
#include <memory>
#include <string_view>

namespace base {

class Settings {
public:
  /// Groups multiple `setValue` calls into a single `QSettings` instance (one disk sync on scope
  /// end).
  class BatchScope {
  public:
    explicit BatchScope(const Settings* owner) : owner_(owner) {
      owner_->enterBatch();
    }
    ~BatchScope() {
      owner_->leaveBatch();
    }
    BatchScope(const BatchScope&) = delete;
    BatchScope& operator=(const BatchScope&) = delete;

  private:
    const Settings* owner_;
  };

protected:
  virtual QString fileName() const = 0;

  QVariant value(QAnyStringView key) const;
  QVariant value(QAnyStringView key, const QVariant& defaultValue) const;
  void setValue(QAnyStringView key, const QVariant& value) const;
  void setValue(QAnyStringView key, const std::string_view value) const;
  bool contains(QAnyStringView key) const;
  void remove(QAnyStringView key) const;

private:
  void enterBatch() const;
  void leaveBatch() const;

  mutable std::unique_ptr<QSettings> batch_;
  mutable int batch_depth_ = 0;
};

}  // namespace base
