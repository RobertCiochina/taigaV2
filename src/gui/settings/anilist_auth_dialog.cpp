/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 */

#include "anilist_auth_dialog.hpp"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "sync/anilist.hpp"
#include "sync/anilist_utils.hpp"
#include "taiga/accounts.hpp"

namespace gui {

AnilistAuthDialog::AnilistAuthDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Sign in with AniList"));
  setModal(true);
  resize(520, 360);

  auto* lay = new QVBoxLayout(this);

  auto* help = new QLabel(
      tr("Click <b>Open AniList</b> and approve Taiga in your browser. After authorization, the "
         "address bar shows a URL whose part after <b>#</b> contains <b>access_token=…</b>. Copy that "
         "full URL from the address bar, or copy only the long token.<br/><br/>"
         "Paste below — Taiga finds the token even if the text includes extra lines."),
      this);
  help->setWordWrap(true);
  help->setTextFormat(Qt::RichText);
  lay->addWidget(help);

  auto* open_btn = new QPushButton(tr("Open AniList…"), this);
  connect(open_btn, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl(QString::fromStdString(sync::anilist::requestTokenUrl())));
  });
  lay->addWidget(open_btn);

  lay->addWidget(new QLabel(tr("Paste URL or token:"), this));

  auto* edit = new QPlainTextEdit(this);
  edit->setPlaceholderText(tr("https://anilist.co/#access_token=…  or  eyJ…"));
  edit->setMinimumHeight(100);
  lay->addWidget(edit, 1);

  auto* status = new QLabel(this);
  status->setWordWrap(true);
  lay->addWidget(status);

  auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  QPushButton* ok_btn = box->button(QDialogButtonBox::Ok);
  ok_btn->setText(tr("Save and verify"));
  lay->addWidget(box);

  connect(box, &QDialogButtonBox::accepted, this, [this, edit, status, box, open_btn, ok_btn] {
    const QString raw = edit->toPlainText();
    const auto token_opt = sync::anilist::extractAnilistAccessToken(raw);
    if (!token_opt || token_opt->empty()) {
      QMessageBox::warning(
          this, tr("AniList sign-in"),
          tr("Could not find an access token. Paste the browser URL after authorizing, or paste the "
             "token only."));
      return;
    }

    const std::string prev_token = taiga::accounts.anilistToken();
    const std::string prev_user = taiga::accounts.anilistUsername();

    taiga::accounts.setAnilistToken(*token_opt);
    sync::anilist::Service::instance()->reloadBearerFromAccounts();

    status->setText(tr("Verifying token…"));
    ok_btn->setEnabled(false);
    box->button(QDialogButtonBox::Cancel)->setEnabled(false);
    open_btn->setEnabled(false);

    const QPointer<AnilistAuthDialog> alive(this);
    sync::anilist::Service::instance()->authenticateUser(
        [this, alive, status, box, open_btn, ok_btn, prev_token, prev_user](bool ok, QString message) {
          if (!alive) return;

          ok_btn->setEnabled(true);
          box->button(QDialogButtonBox::Cancel)->setEnabled(true);
          open_btn->setEnabled(true);

          if (!ok) {
            taiga::accounts.setAnilistToken(prev_token);
            taiga::accounts.setAnilistUsername(prev_user);
            sync::anilist::Service::instance()->reloadBearerFromAccounts();
            status->setText(tr("Verification failed."));
            QMessageBox::warning(this, tr("AniList sign-in"),
                                 message.isEmpty() ? tr("Unknown error.") : message);
            return;
          }

          status->setText(tr("Signed in."));
          accept();
        });
  });

  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool AnilistAuthDialog::signIn(QWidget* parent) {
  AnilistAuthDialog dlg(parent);
  return dlg.exec() == QDialog::Accepted;
}

}  // namespace gui
