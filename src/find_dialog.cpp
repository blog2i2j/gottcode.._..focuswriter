/*
	SPDX-FileCopyrightText: 2008-2025 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "find_dialog.h"

#include "document.h"
#include "smart_quotes.h"
#include "stack.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>

//-----------------------------------------------------------------------------

FindDialog::FindDialog(Stack* documents)
	: QDialog(documents->window(), Qt::WindowTitleHint | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint)
	, m_documents(documents)
{
	// Create widgets
	QLabel* find_label = new QLabel(tr("Search for:"), this);
	m_find_string = new QLineEdit(this);
	m_replace_label = new QLabel(tr("Replace with:"), this);
	m_replace_string = new QLineEdit(this);
	connect(m_find_string, &QLineEdit::textChanged, this, &FindDialog::findChanged);

	m_ignore_case = new QCheckBox(tr("Ignore case"), this);
	m_whole_words = new QCheckBox(tr("Whole words only"), this);
	m_regular_expressions = new QCheckBox(tr("Regular expressions"), this);
	connect(m_regular_expressions, &QCheckBox::toggled, m_whole_words, &QCheckBox::setDisabled);

	m_search_backwards = new QRadioButton(tr("Search up"), this);
	QRadioButton* search_forwards = new QRadioButton(tr("Search down"), this);
	search_forwards->setChecked(true);

	// Create buttons
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &FindDialog::reject);

	m_find_button = buttons->addButton(tr("&Find"), QDialogButtonBox::ActionRole);
	m_find_button->setEnabled(false);
	m_find_button->setDefault(true);
	connect(m_find_button, &QPushButton::clicked, this, qOverload<>(&FindDialog::find));

	m_replace_button = buttons->addButton(tr("&Replace"), QDialogButtonBox::ActionRole);
	m_replace_button->setEnabled(false);
	m_replace_button->setAutoDefault(false);
	connect(m_replace_button, &QAbstractButton::clicked, this, &FindDialog::replace);

	m_replace_all_button = buttons->addButton(tr("Replace &All"), QDialogButtonBox::ActionRole);
	m_replace_all_button->setEnabled(false);
	m_replace_all_button->setAutoDefault(false);
	connect(m_replace_all_button, &QPushButton::clicked, this, &FindDialog::replaceAll);

	if (!buttons->button(QDialogButtonBox::Close)->icon().isNull()) {
		m_find_button->setIcon(QIcon::fromTheme("edit-find"));
		m_replace_button->setIcon(QIcon::fromTheme("edit-find-replace"));
	}

	// Lay out dialog
	QGridLayout* layout = new QGridLayout(this);
	layout->setColumnStretch(1, 1);
	layout->addWidget(find_label, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
	layout->addWidget(m_find_string, 0, 1, 1, 2);
	layout->addWidget(m_replace_label, 1, 0, Qt::AlignRight | Qt::AlignVCenter);
	layout->addWidget(m_replace_string, 1, 1, 1, 2);
	layout->addWidget(m_ignore_case, 2, 1);
	layout->addWidget(m_whole_words, 3, 1);
	layout->addWidget(m_regular_expressions, 4, 1);
	layout->addWidget(m_search_backwards, 2, 2);
	layout->addWidget(search_forwards, 3, 2);
	layout->addWidget(buttons, 5, 0, 1, 3);
	setFixedWidth(sizeHint().width());

	// Load settings
	const QSettings settings;
	m_ignore_case->setChecked(!settings.value("FindDialog/CaseSensitive", false).toBool());
	m_whole_words->setChecked(settings.value("FindDialog/WholeWords", false).toBool());
	m_regular_expressions->setChecked(settings.value("FindDialog/RegularExpressions", false).toBool());
	m_search_backwards->setChecked(settings.value("FindDialog/SearchBackwards", false).toBool());

	m_find_string->installEventFilter(this);
	m_replace_string->installEventFilter(this);
}

//-----------------------------------------------------------------------------

bool FindDialog::eventFilter(QObject* watched, QEvent* event)
{
	if ((event->type() == QEvent::KeyPress) && qobject_cast<QLineEdit*>(watched)) {
		if (SmartQuotes::isEnabled() && SmartQuotes::insert(static_cast<QLineEdit*>(watched), static_cast<QKeyEvent*>(event))) {
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

//-----------------------------------------------------------------------------

void FindDialog::findNext()
{
	find(false);
}

//-----------------------------------------------------------------------------

void FindDialog::findPrevious()
{
	find(true);
}

//-----------------------------------------------------------------------------

void FindDialog::reject()
{
	QSettings settings;
	settings.setValue("FindDialog/CaseSensitive", !m_ignore_case->isChecked());
	settings.setValue("FindDialog/WholeWords", m_whole_words->isChecked());
	settings.setValue("FindDialog/RegularExpressions", m_regular_expressions->isChecked());
	settings.setValue("FindDialog/SearchBackwards", m_search_backwards->isChecked());
	QDialog::reject();
}

//-----------------------------------------------------------------------------

void FindDialog::showFindMode()
{
	setWindowTitle(tr("Find"));
	showMode(false);
}

//-----------------------------------------------------------------------------

void FindDialog::showReplaceMode()
{
	setWindowTitle(tr("Replace"));
	showMode(true);
}

//-----------------------------------------------------------------------------

void FindDialog::moveEvent(QMoveEvent* event)
{
	m_position = pos();
	QDialog::moveEvent(event);
}

//-----------------------------------------------------------------------------

void FindDialog::showEvent(QShowEvent* event)
{
	if (!m_position.isNull()) {
		const QRect rect(m_position, sizeHint());
		if (screen()->availableGeometry().contains(rect)) {
			move(m_position);
		}
	}
	QDialog::showEvent(event);
}

//-----------------------------------------------------------------------------

void FindDialog::find()
{
	find(m_search_backwards->isChecked());
}

//-----------------------------------------------------------------------------

void FindDialog::findChanged(const QString& text)
{
	const bool enabled = !text.isEmpty();
	m_find_button->setEnabled(enabled);
	m_replace_button->setEnabled(enabled);
	m_replace_all_button->setEnabled(enabled);
	Q_EMIT findNextAvailable(enabled);
}

//-----------------------------------------------------------------------------

void FindDialog::replace()
{
	const QString text = m_find_string->text();
	if (text.isEmpty()) {
		return;
	}

	QTextEdit* document = m_documents->currentDocument()->text();
	QTextCursor cursor = document->textCursor();
	if (!m_regular_expressions->isChecked()) {
		if (QString::compare(cursor.selectedText(), text, m_ignore_case->isChecked() ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0) {
			cursor.insertText(m_replace_string->text());
			document->setTextCursor(cursor);
		}
	} else {
		const QRegularExpression regex("^" + text + "$", m_ignore_case->isChecked() ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
		QString match = cursor.selectedText();
		if (regex.match(match).hasMatch()) {
			match.replace(regex, m_replace_string->text());
			cursor.insertText(match);
			document->setTextCursor(cursor);
		}
	}

	find();
}

//-----------------------------------------------------------------------------

void FindDialog::replaceAll()
{
	const QString text = m_find_string->text();
	if (text.isEmpty()) {
		return;
	}
	const QRegularExpression regex(text, m_ignore_case->isChecked() ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);

	QTextDocument::FindFlags flags;
	if (!m_ignore_case->isChecked()) {
		flags |= QTextDocument::FindCaseSensitively;
	}
	if (m_whole_words->isChecked() && !m_regular_expressions->isChecked()) {
		flags |= QTextDocument::FindWholeWords;
	}

	// Count instances
	int found = 0;
	QTextEdit* document = m_documents->currentDocument()->text();
	QTextCursor cursor = document->textCursor();
	cursor.movePosition(QTextCursor::Start);
	if (!m_regular_expressions->isChecked()) {
		Q_FOREVER {
			cursor = document->document()->find(text, cursor, flags);
			if (!cursor.isNull()) {
				found++;
			} else {
				break;
			}
		}
	} else {
		Q_FOREVER {
			cursor = document->document()->find(regex, cursor, flags);
			if (!cursor.isNull() && cursor.hasSelection()) {
				found++;
			} else {
				break;
			}
		}
	}
	if (found) {
		if (QMessageBox::question(this,
				tr("Question"),
				tr("Replace %n instance(s)?", "", found),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No) {
			return;
		}
	} else {
		QMessageBox::information(this, tr("Sorry"), tr("Phrase not found."));
		return;
	}

	// Replace instances
	QTextCursor start_cursor = document->textCursor();
	start_cursor.beginEditBlock();
	if (!m_regular_expressions->isChecked()) {
		Q_FOREVER {
			cursor = document->document()->find(text, cursor, flags);
			if (!cursor.isNull()) {
				cursor.insertText(m_replace_string->text());
			} else {
				break;
			}
		}
	} else {
		Q_FOREVER {
			cursor = document->document()->find(regex, cursor, flags);
			if (!cursor.isNull() && cursor.hasSelection()) {
				QString match = cursor.selectedText();
				match.replace(regex, m_replace_string->text());
				cursor.insertText(match);
			} else {
				break;
			}
		}
	}
	start_cursor.endEditBlock();
	document->setTextCursor(start_cursor);
}

//-----------------------------------------------------------------------------

void FindDialog::find(bool backwards)
{
	const QString text = m_find_string->text();
	if (text.isEmpty()) {
		return;
	}
	const QRegularExpression regex(text, m_ignore_case->isChecked() ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);

	QTextDocument::FindFlags flags;
	if (!m_ignore_case->isChecked()) {
		flags |= QTextDocument::FindCaseSensitively;
	}
	if (m_whole_words->isChecked() && !m_regular_expressions->isChecked()) {
		flags |= QTextDocument::FindWholeWords;
	}
	if (backwards) {
		flags |= QTextDocument::FindBackward;
	}

	QTextEdit* document = m_documents->currentDocument()->text();
	QTextCursor cursor = document->textCursor();
	if (!m_regular_expressions->isChecked()) {
		cursor = document->document()->find(text, cursor, flags);
	} else {
		cursor = document->document()->find(regex, cursor, flags);
	}
	if (cursor.isNull()) {
		cursor = document->textCursor();
		cursor.movePosition(!backwards ? QTextCursor::Start : QTextCursor::End);
		if (!m_regular_expressions->isChecked()) {
			cursor = document->document()->find(text, cursor, flags);
		} else {
			cursor = document->document()->find(regex, cursor, flags);
		}
	}

	if (!cursor.isNull()) {
		document->setTextCursor(cursor);
	} else {
		QMessageBox::information(this, tr("Sorry"), tr("Phrase not found."));
	}
}

//-----------------------------------------------------------------------------

void FindDialog::showMode(bool replace)
{
	m_replace_label->setVisible(replace);
	m_replace_string->setVisible(replace);
	m_replace_button->setVisible(replace);
	m_replace_all_button->setVisible(replace);
	setFixedHeight(sizeHint().height());

	if (!m_regular_expressions->isChecked()) {
		QString text = m_documents->currentDocument()->text()->textCursor().selectedText().trimmed();
		text.remove(0, text.lastIndexOf(QChar(0x2029)) + 1);
		if (!text.isEmpty()) {
			m_find_string->setText(text);
		}
	}
	m_find_string->setFocus();

	show();
	activateWindow();
}

//-----------------------------------------------------------------------------
