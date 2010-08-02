/***********************************************************************
 *
 * Copyright (C) 2008, 2009, 2010 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "window.h"

#include "document.h"
#include "load_screen.h"
#include "preferences.h"
#include "preferences_dialog.h"
#include "session.h"
#include "session_manager.h"
#include "stack.h"
#include "theme.h"
#include "theme_manager.h"
#include "timer_display.h"
#include "timer_manager.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDate>
#include <QFileDialog>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>

/*****************************************************************************/

namespace
{
	QKeySequence keyBinding(const QKeySequence& shortcut, const QKeySequence& back)
	{
		if (!shortcut.isEmpty()) {
			return shortcut;
		} else {
			return back;
		}
	}
}

/*****************************************************************************/

Window::Window()
: m_toolbar(0),
  m_margin(0),
  m_fullscreen(true),
  m_auto_save(true),
  m_save_positions(true),
  m_goal_type(0),
  m_time_goal(0),
  m_wordcount_goal(0),
  m_current_time(0),
  m_current_wordcount(0) {
	setAttribute(Qt::WA_DeleteOnClose);
	setContextMenuPolicy(Qt::NoContextMenu);
	setWindowIcon(QIcon(":/focuswriter.png"));

	// Set up icons
	QStringList paths = QIcon::themeSearchPaths();
	paths.removeAll(":/icons");
	paths.prepend(":/icons");
	QIcon::setThemeSearchPaths(paths);
	if (QIcon::themeName().isEmpty()) {
		QIcon::setThemeName("hicolor");
		setIconSize(QSize(22,22));
	}

	// Create window contents first so they stack behind documents
	menuBar();
	m_toolbar = new QToolBar(this);
	m_toolbar->setFloatable(false);
	m_toolbar->setMovable(false);
	addToolBar(m_toolbar);
	QWidget* contents = new QWidget(this);
	setCentralWidget(contents);

	// Create documents
	m_documents = new Stack(this);
	m_sessions = new SessionManager(this);
	m_timers = new TimerManager(m_documents, this);
	connect(m_documents, SIGNAL(updateFormatActions()), this, SLOT(updateFormatActions()));
	connect(m_sessions, SIGNAL(themeChanged(Theme)), m_documents, SLOT(themeSelected(Theme)));

	// Set up menubar and toolbar
	initMenus();

	// Set up details
	m_footer = new QWidget(contents);
	QWidget* details = new QWidget(m_footer);
	m_wordcount_label = new QLabel(tr("Words: 0"), details);
	m_page_label = new QLabel(tr("Pages: 0"), details);
	m_paragraph_label = new QLabel(tr("Paragraphs: 0"), details);
	m_character_label = new QLabel(tr("Characters: 0"), details);
	m_progress_label = new QLabel(tr("0% of daily goal"), details);
	m_clock_label = new QLabel(details);
	updateClock();

	// Set up clock
	m_clock_timer = new QTimer(this);
	m_clock_timer->setInterval(60000);
	connect(m_clock_timer, SIGNAL(timeout()), this, SLOT(updateClock()));
	connect(m_clock_timer, SIGNAL(timeout()), m_timers, SLOT(saveTimers()));
	int delay = (60 - QTime::currentTime().second()) * 1000;
	QTimer::singleShot(delay, m_clock_timer, SLOT(start()));
	QTimer::singleShot(delay, this, SLOT(updateClock()));

	// Set up tabs
	m_tabs = new QTabBar(m_footer);
	m_tabs->setShape(QTabBar::RoundedSouth);
	m_tabs->setDocumentMode(true);
	m_tabs->setExpanding(false);
	m_tabs->setMovable(true);
	m_tabs->setTabsClosable(true);
	m_tabs->setUsesScrollButtons(true);
	connect(m_tabs, SIGNAL(currentChanged(int)), this, SLOT(tabClicked(int)));
	connect(m_tabs, SIGNAL(tabCloseRequested(int)), this, SLOT(tabClosed(int)));
	connect(m_tabs, SIGNAL(tabMoved(int, int)), this, SLOT(tabMoved(int, int)));
	new QShortcut(QKeySequence::NextChild, this, SLOT(nextDocument()));
	new QShortcut(QKeySequence::PreviousChild, this, SLOT(previousDocument()));

	// Lay out details
	QHBoxLayout* clock_layout = new QHBoxLayout;
	clock_layout->setMargin(0);
	clock_layout->setSpacing(6);
	clock_layout->addWidget(m_timers->display(), 0, Qt::AlignCenter);
	clock_layout->addWidget(m_clock_label);

	QHBoxLayout* details_layout = new QHBoxLayout(details);
	details_layout->setSpacing(25);
	details_layout->setMargin(6);
	details_layout->addWidget(m_wordcount_label);
	details_layout->addWidget(m_page_label);
	details_layout->addWidget(m_paragraph_label);
	details_layout->addWidget(m_character_label);
	details_layout->addStretch();
	details_layout->addWidget(m_progress_label);
	details_layout->addStretch();
	details_layout->addLayout(clock_layout);

	// Lay out footer
	QVBoxLayout* footer_layout = new QVBoxLayout(m_footer);
	footer_layout->setSpacing(0);
	footer_layout->setMargin(0);
	footer_layout->addWidget(details);
	footer_layout->addWidget(m_tabs);

	// Lay out window
	QVBoxLayout* layout = new QVBoxLayout(contents);
	layout->setSpacing(0);
	layout->setMargin(0);
	layout->addStretch();
	layout->addWidget(m_footer);

	// Load current daily progress
	QSettings settings;
	if (settings.value("Progress/Date").toDate() != QDate::currentDate()) {
		settings.remove("Progress");
	}
	settings.setValue("Progress/Date", QDate::currentDate().toString(Qt::ISODate));
	m_current_wordcount = settings.value("Progress/Words", 0).toInt();
	m_current_time = settings.value("Progress/Time", 0).toInt();
	updateProgress();

	// Load settings
	Preferences preferences;
	loadPreferences(preferences);
	m_documents->themeSelected(settings.value("ThemeManager/Theme").toString());

	// Restore window geometry
	setMinimumSize(640, 480);
	restoreGeometry(settings.value("Window/Geometry").toByteArray());
	m_fullscreen = !settings.value("Window/Fullscreen", true).toBool();
	toggleFullscreen();
	show();

	// Update margin
	m_tabs->blockSignals(true);
	m_tabs->addTab(tr("Untitled"));
	updateMargin();
	m_tabs->removeTab(0);
	m_tabs->blockSignals(false);

	// Open previous documents
	QString session = settings.value("SessionManager/Session").toString();
	QStringList files = QApplication::arguments().mid(1);
	if (!files.isEmpty()) {
		session.clear();
		settings.setValue("Save/Current", files);
		settings.setValue("Save/Positions", QStringList());
		settings.setValue("Save/Active", 0);
	}
	m_sessions->setCurrent(session);
}

/*****************************************************************************/

void Window::addDocuments(const QStringList& files, const QStringList& positions, int active) {
	m_documents->loadScreen()->startStep("");

	if (!files.isEmpty()) {
		for (int i = 0; i < files.count(); ++i) {
			addDocument(files.at(i), positions.value(i, "-1").toInt());
		}
	} else {
		newDocument();
	}
	m_tabs->setCurrentIndex(active);

	m_documents->loadScreen()->finishStep();
}

/*****************************************************************************/

bool Window::closeDocuments(QSettings* session) {
	if (m_documents->count() == 0) {
		return true;
	}

	// Save files
	int active = m_tabs->currentIndex();
	QStringList files;
	QStringList positions;
	for (int i = 0; i < m_documents->count(); ++i) {
		Document* document = m_documents->document(i);
		QString filename = document->filename();
		if (!filename.isEmpty()) {
			files.append(filename);
			positions.append(QString::number(document->text()->textCursor().position()));
		}

		m_tabs->setCurrentIndex(i);
		if (!saveDocument(i)) {
			m_tabs->setCurrentIndex(active);
			return false;
		}
	}

	// Store current files
	session->setValue("Save/Current", files);
	session->setValue("Save/Positions", positions);
	session->setValue("Save/Active", active);

	// Close files
	int count = m_documents->count();
	for (int i = 0; i < count; ++i) {
		m_documents->removeDocument(0);
		m_tabs->removeTab(0);
	}

	return true;
}

/*****************************************************************************/

bool Window::event(QEvent* event) {
	if (event->type() == QEvent::WindowBlocked) {
		hideInterface();
	}
	return QMainWindow::event(event);
}

/*****************************************************************************/

void Window::closeEvent(QCloseEvent* event) {
	if (!m_timers->cancelEditing() || !m_sessions->closeCurrent()) {
		event->ignore();
		return;
	}
	if (!m_fullscreen) {
		QSettings().setValue("Window/Geometry", saveGeometry());
	}
	QMainWindow::closeEvent(event);
}

/*****************************************************************************/

void Window::leaveEvent(QEvent* event) {
	if (qApp->activePopupWidget() == 0) {
		hideInterface();
	}
	QMainWindow::leaveEvent(event);
}

/*****************************************************************************/

void Window::resizeEvent(QResizeEvent* event) {
	if (!m_fullscreen) {
		QSettings().setValue("Window/Geometry", saveGeometry());
	}
	m_documents->resize(size());
	QMainWindow::resizeEvent(event);
}

/*****************************************************************************/

void Window::newDocument() {
	addDocument();
	m_actions["Rename"]->setEnabled(false);
}

/*****************************************************************************/

void Window::openDocument() {
	QString filename = QFileDialog::getOpenFileName(window(), tr("Open File"), QString(), m_open_filter);
	if (!filename.isEmpty()) {
		Document* document = m_documents->currentDocument();
		int index = (document->index() && !document->text()->document()->isModified()) ? m_documents->currentIndex() : -1;
		addDocument(filename);
		if (index != -1) {
			m_tabs->setCurrentIndex(index);
			closeDocument();
		}
	}
}

/*****************************************************************************/

void Window::renameDocument() {
	if (m_documents->currentDocument()->rename()) {
		updateTab(m_documents->currentIndex());
	}
}

/*****************************************************************************/

void Window::saveAllDocuments() {
	int index = m_tabs->currentIndex();
	for (int i = 0; i < m_documents->count(); ++i) {
		Document* document = m_documents->document(i);
		if (!document->filename().isEmpty()) {
			document->save();
		} else {
			m_tabs->setCurrentIndex(i);
			document->saveAs();
		}
	}
	m_tabs->setCurrentIndex(index);
}

/*****************************************************************************/

void Window::closeDocument() {
	int index = m_documents->currentIndex();
	if (!saveDocument(index)) {
		return;
	}
	if (m_documents->count() == 1) {
		newDocument();
	}
	m_documents->removeDocument(index);
	m_tabs->removeTab(index);
}

/*****************************************************************************/

void Window::nextDocument() {
	int index = m_tabs->currentIndex() + 1;
	if (index >= m_tabs->count()) {
		index = 0;
	}
	m_tabs->setCurrentIndex(index);
}

/*****************************************************************************/

void Window::previousDocument() {
	int index = m_tabs->currentIndex() - 1;
	if (index < 0) {
		index = m_tabs->count() - 1;
	}
	m_tabs->setCurrentIndex(index);
}

/*****************************************************************************/

void Window::toggleFullscreen() {
	m_fullscreen = !m_fullscreen;
	QSettings().setValue("Window/Fullscreen", m_fullscreen);

	if (m_fullscreen) {
		setWindowState(windowState() | Qt::WindowFullScreen);
	} else {
		setWindowState(windowState() & ~Qt::WindowFullScreen);
	}
	m_actions["Fullscreen"]->setChecked(m_fullscreen);
}

/*****************************************************************************/

void Window::toggleToolbar(bool visible) {
	m_toolbar->setVisible(visible);
	QSettings().setValue("Toolbar/Shown", visible);
	updateMargin();
}

/*****************************************************************************/

void Window::themeClicked() {
	ThemeManager manager(*m_sessions->current()->data(), this);
	connect(&manager, SIGNAL(themeSelected(const Theme&)), m_documents, SLOT(themeSelected(const Theme&)));
	manager.exec();
}

/*****************************************************************************/

void Window::preferencesClicked() {
	Preferences preferences;
	PreferencesDialog dialog(preferences, this);
	if (dialog.exec() == QDialog::Accepted) {
		loadPreferences(preferences);
	}
}

/*****************************************************************************/

void Window::aboutClicked() {
	QMessageBox::about(this, tr("About FocusWriter"), tr(
		"<p><center><big><b>FocusWriter %1</b></big><br/>"
		"A simple fullscreen word processor<br/>"
		"<small>Copyright &copy; 2008-2010 Graeme Gott</small><br/>"
		"<small>Released under the <a href=\"http://www.gnu.org/licenses/gpl.html\">GPL 3</a> license</small></center></p>"
		"<p><center>Includes <a href=\"http://hunspell.sourceforge.net/\">Hunspell</a> 1.2.11 for spell checking<br/>"
		"<small>Used under the <a href=\"http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html\">LGPL 2.1</a> license</small></center></p>"
		"<p><center>Includes icons from the <a href=\"http://www.oxygen-icons.org/\">Oxygen</a> icon theme<br/>"
		"<small>Used under the <a href=\"http://www.gnu.org/licenses/lgpl.html\">LGPL 3</a> license</small></center></p>"
	).arg(QApplication::applicationVersion()));
}

/*****************************************************************************/

void Window::tabClicked(int index) {
	if (m_documents->count() == 0) {
		return;
	}
	m_documents->setCurrentDocument(index);
	updateDetails();
	updateSave();
	m_documents->currentDocument()->text()->setFocus();
}

/*****************************************************************************/

void Window::tabClosed(int index) {
	m_tabs->setCurrentIndex(index);
	closeDocument();
}

/*****************************************************************************/

void Window::tabMoved(int from, int to) {
	m_documents->moveDocument(from, to);
	m_documents->setCurrentDocument(m_tabs->currentIndex());
}

/*****************************************************************************/

void Window::updateClock() {
	m_clock_label->setText(QTime::currentTime().toString(Qt::DefaultLocaleShortDate));
}

/*****************************************************************************/

void Window::updateDetails() {
	Document* document = m_documents->currentDocument();
	m_character_label->setText(tr("Characters: %L1 / %L2").arg(document->characterCount()).arg(document->characterAndSpaceCount()));
	m_page_label->setText(tr("Pages: %L1").arg(document->pageCount()));
	m_paragraph_label->setText(tr("Paragraphs: %L1").arg(document->paragraphCount()));
	m_wordcount_label->setText(tr("Words: %L1").arg(document->wordCount()));
}

/*****************************************************************************/

void Window::updateFormatActions() {
	Document* document = m_documents->currentDocument();
	if (!document) {
		return;
	}

	bool enabled = document->isRichText();
	foreach (QAction* action, m_format_actions) {
		action->setEnabled(enabled);
	}
	if (enabled) {
		m_richtext_action->setVisible(false);
		m_plaintext_action->setVisible(true);
	} else {
		m_plaintext_action->setVisible(false);
		m_richtext_action->setVisible(true);
	}
	m_actions["FormatIndentDecrease"]->setEnabled(document->text()->textCursor().blockFormat().indent() > 0);

	QTextCharFormat format = document->text()->currentCharFormat();
	m_actions["FormatBold"]->setChecked(format.fontWeight() == QFont::Bold);
	m_actions["FormatItalic"]->setChecked(format.fontItalic());
	m_actions["FormatStrikeOut"]->setChecked(format.fontStrikeOut());
	m_actions["FormatUnderline"]->setChecked(format.fontUnderline());
	m_actions["FormatSuperScript"]->setChecked(format.verticalAlignment() == QTextCharFormat::AlignSuperScript);
	m_actions["FormatSubScript"]->setChecked(format.verticalAlignment() == QTextCharFormat::AlignSubScript);

	Qt::Alignment alignment = document->text()->alignment();
	if (alignment & Qt::AlignLeft) {
		m_actions["FormatAlignLeft"]->setChecked(true);
	} else if (alignment & Qt::AlignRight) {
		m_actions["FormatAlignRight"]->setChecked(true);
	} else if (alignment & Qt::AlignCenter) {
		m_actions["FormatAlignCenter"]->setChecked(true);
	} else if (alignment & Qt::AlignJustify) {
		m_actions["FormatAlignJustify"]->setChecked(true);
	}
}

/*****************************************************************************/

void Window::updateProgress() {
	int progress = 0;
	if (m_goal_type == 1) {
		progress = (m_current_time * 100) / (m_time_goal * 60000);
	} else if (m_goal_type == 2) {
		progress = (m_current_wordcount * 100) / m_wordcount_goal;
	}
	m_progress_label->setText(tr("%1% of daily goal").arg(progress));
}

/*****************************************************************************/

void Window::updateSave() {
	m_actions["Save"]->setEnabled(m_documents->currentDocument()->text()->document()->isModified());
	m_actions["Rename"]->setDisabled(m_documents->currentDocument()->filename().isEmpty());
	for (int i = 0; i < m_documents->count(); ++i) {
		updateTab(i);
	}
}

/*****************************************************************************/

void Window::addDocument(const QString& filename, int position) {
	if (!filename.isEmpty()) {
		for (int i = 0; i < m_documents->count(); ++i) {
			if (m_documents->document(i)->filename() == filename) {
				m_tabs->setCurrentIndex(i);
				return;
			}
		}
	}

	bool show_load = !filename.isEmpty() && (QFileInfo(filename).size() > 100000 || m_documents->loadScreen()->isVisible());
	if (show_load) {
		m_documents->loadScreen()->startStep(tr("Opening %1").arg(filename));
	}

	Document* document = new Document(filename, m_current_wordcount, m_current_time, m_margin, m_sessions->current()->theme(), this);
	connect(document, SIGNAL(changed()), this, SLOT(updateDetails()));
	connect(document, SIGNAL(changed()), this, SLOT(updateProgress()));
	connect(document, SIGNAL(changedName()), this, SLOT(updateSave()));
	connect(document, SIGNAL(footerVisible(bool)), m_timers->display(), SLOT(setVisible(bool)));
	connect(document, SIGNAL(indentChanged(bool)), m_actions["FormatIndentDecrease"], SLOT(setEnabled(bool)));
	connect(document->text()->document(), SIGNAL(modificationChanged(bool)), this, SLOT(updateSave()));

	// Restore cursor position
	QTextCursor cursor = document->text()->textCursor();
	if (m_save_positions && position != -1) {
		cursor.setPosition(position);
	} else {
		cursor.movePosition(QTextCursor::End);
	}
	document->text()->setTextCursor(cursor);

	m_documents->addDocument(document);

	int index = m_tabs->addTab(tr("Untitled"));
	updateTab(index);
	m_tabs->setCurrentIndex(index);

	if (show_load) {
		m_documents->loadScreen()->finishStep();
	}
}

/*****************************************************************************/

bool Window::saveDocument(int index) {
	Document* document = m_documents->document(index);
	if (!document->text()->document()->isModified()) {
		return true;
	}

	// Auto-save document
	if (m_auto_save && document->text()->document()->isModified() && !document->filename().isEmpty()) {
		document->save();
		return true;
	}

	// Prompt about saving changes
	switch (QMessageBox::question(this, tr("Question"), tr("Save changes?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)) {
	case QMessageBox::Save:
		return document->save();
	case QMessageBox::Discard:
		document->text()->document()->setModified(false);
		return true;
	case QMessageBox::Cancel:
	default:
		return false;
	}
}

/*****************************************************************************/

void Window::loadPreferences(const Preferences& preferences) {
	m_auto_save = preferences.autoSave();
	if (m_auto_save) {
		connect(m_clock_timer, SIGNAL(timeout()), m_documents, SLOT(autoSave()));
	} else {
		disconnect(m_clock_timer, SIGNAL(timeout()), m_documents, SLOT(autoSave()));
	}
	m_save_positions = preferences.savePositions();

	QString plaintext = tr("Plain Text (*.txt)");
	QString richtext = tr("FocusWriter Rich Text (*.fwr)");
	QString all = tr("All Files (*)");
	m_open_filter = preferences.richText() ? (richtext + ";;" + plaintext+ ";;" + all) : (plaintext + ";;" + richtext+ ";;" + all);

	m_character_label->setVisible(preferences.showCharacters());
	m_page_label->setVisible(preferences.showPages());
	m_paragraph_label->setVisible(preferences.showParagraphs());
	m_wordcount_label->setVisible(preferences.showWords());
	m_progress_label->setVisible(preferences.goalType() != 0);

	m_goal_type = preferences.goalType();
	m_wordcount_goal = preferences.goalWords();
	m_time_goal = preferences.goalMinutes();
	updateProgress();

	m_toolbar->clear();
	m_toolbar->hide();
	m_toolbar->setToolButtonStyle(Qt::ToolButtonStyle(preferences.toolbarStyle()));
	QStringList actions = preferences.toolbarActions();
	foreach (const QString action, actions) {
		if (action == "|") {
			m_toolbar->addSeparator();
		} else if (!action.startsWith("^")) {
			m_toolbar->addAction(m_actions.value(action));
		}
	}
	m_toolbar->setVisible(QSettings().value("Toolbar/Shown", true).toBool());
	updateMargin();

	for (int i = 0; i < m_documents->count(); ++i) {
		m_documents->document(i)->loadPreferences(preferences);
	}
	if (m_documents->count() > 0) {
		updateDetails();
	}
}

/*****************************************************************************/

void Window::hideInterface() {
	m_documents->setFooterVisible(false);
	m_documents->setHeaderVisible(false);
	for (int i = 0; i < m_documents->count(); ++i) {
		m_documents->document(i)->setScrollBarVisible(false);
	}
}

/*****************************************************************************/

void Window::updateMargin() {
	int header = 0;
	if (m_toolbar->isVisible()) {
		header = m_toolbar->mapToParent(m_toolbar->rect().bottomLeft()).y() + 1;
	} else if (menuBar()->window() == this) {
		header = menuBar()->mapToParent(menuBar()->rect().bottomLeft()).y() + 1;
	}
	int footer = m_footer->sizeHint().height();
	m_margin = qMax(header, footer);

	for (int i = 0; i < m_documents->count(); ++i) {
		m_documents->document(i)->setMargin(m_margin);
	}
	m_documents->setMargins(footer, header);
}

/*****************************************************************************/

void Window::updateTab(int index) {
	Document* document = m_documents->document(index);
	QString filename = document->filename();
	if (filename.isEmpty()) {
		filename = tr("(Untitled %1)").arg(document->index());
	}
	bool modified = document->text()->document()->isModified();
	m_tabs->setTabText(index, QFileInfo(filename).fileName() + (modified ? "*" : ""));
	m_tabs->setTabToolTip(index, QDir::toNativeSeparators(filename));
	if (document == m_documents->currentDocument()) {
		setWindowFilePath(filename);
		setWindowModified(modified);
	}
}

/*****************************************************************************/

void Window::initMenus() {
	// Create file menu
	QMenu* file_menu = menuBar()->addMenu(tr("&File"));
	m_actions["New"] = file_menu->addAction(QIcon::fromTheme("document-new"), tr("&New"), this, SLOT(newDocument()), QKeySequence::New);
	m_actions["Open"] = file_menu->addAction(QIcon::fromTheme("document-open"), tr("&Open..."), this, SLOT(openDocument()), QKeySequence::Open);
	file_menu->addSeparator();
	m_actions["Save"] = file_menu->addAction(QIcon::fromTheme("document-save"), tr("&Save"), m_documents, SLOT(save()), QKeySequence::Save);
	m_actions["Save"]->setEnabled(false);
	m_actions["SaveAs"] = file_menu->addAction(QIcon::fromTheme("document-save-as"), tr("Save &As..."), m_documents, SLOT(saveAs()), QKeySequence::SaveAs);
	m_actions["Rename"] = file_menu->addAction(QIcon::fromTheme("edit-rename"), tr("&Rename..."), this, SLOT(renameDocument()));
	m_actions["Rename"]->setEnabled(false);
	m_actions["SaveAll"] = file_menu->addAction(QIcon::fromTheme("document-save-all"), tr("Save A&ll"), this, SLOT(saveAllDocuments()));
	file_menu->addSeparator();
	file_menu->addMenu(m_sessions->menu());
	m_actions["ManageSessions"] = new QAction(QIcon::fromTheme("view-choose"), tr("Manage Sessions"), this);
	connect(m_actions["ManageSessions"], SIGNAL(triggered()), m_sessions, SLOT(exec()));
	file_menu->addSeparator();
	m_actions["Print"] = file_menu->addAction(QIcon::fromTheme("document-print"), tr("&Print..."), m_documents, SLOT(print()), QKeySequence::Print);
	file_menu->addSeparator();
	m_actions["Close"] = file_menu->addAction(QIcon::fromTheme("window-close"), tr("&Close"), this, SLOT(closeDocument()), QKeySequence::Close);
	m_actions["Quit"] = file_menu->addAction(QIcon::fromTheme("application-exit"), tr("&Quit"), this, SLOT(close()), keyBinding(QKeySequence::Quit, tr("Ctrl+Q")));

	// Create edit menu
	QMenu* edit_menu = menuBar()->addMenu(tr("&Edit"));
	m_actions["Undo"] = edit_menu->addAction(QIcon::fromTheme("edit-undo"), tr("&Undo"), m_documents, SLOT(undo()), QKeySequence::Undo);
	m_actions["Undo"]->setEnabled(false);
	connect(m_documents, SIGNAL(undoAvailable(bool)), m_actions["Undo"], SLOT(setEnabled(bool)));
	m_actions["Redo"] = edit_menu->addAction(QIcon::fromTheme("edit-redo"), tr("&Redo"), m_documents, SLOT(redo()), QKeySequence::Redo);
	m_actions["Redo"]->setEnabled(false);
	connect(m_documents, SIGNAL(redoAvailable(bool)), m_actions["Redo"], SLOT(setEnabled(bool)));
	edit_menu->addSeparator();
	m_actions["Cut"] = edit_menu->addAction(QIcon::fromTheme("edit-cut"), tr("Cu&t"), m_documents, SLOT(cut()), QKeySequence::Cut);
	m_actions["Cut"]->setEnabled(false);
	connect(m_documents, SIGNAL(copyAvailable(bool)), m_actions["Cut"], SLOT(setEnabled(bool)));
	m_actions["Copy"] = edit_menu->addAction(QIcon::fromTheme("edit-copy"), tr("&Copy"), m_documents, SLOT(copy()), QKeySequence::Copy);
	m_actions["Copy"]->setEnabled(false);
	connect(m_documents, SIGNAL(copyAvailable(bool)), m_actions["Copy"], SLOT(setEnabled(bool)));
	m_actions["Paste"] = edit_menu->addAction(QIcon::fromTheme("edit-paste"), tr("&Paste"), m_documents, SLOT(paste()), QKeySequence::Paste);
	edit_menu->addSeparator();
	m_actions["SelectAll"] = edit_menu->addAction(QIcon::fromTheme("edit-select-all"), tr("Select &All"), m_documents, SLOT(selectAll()), QKeySequence::SelectAll);

	// Create format menu
	QMenu* format_menu = menuBar()->addMenu(tr("&Format"));
	m_actions["FormatBold"] = format_menu->addAction(QIcon::fromTheme("format-text-bold"), tr("&Bold"), m_documents, SLOT(setFontBold(bool)), QKeySequence::Bold);
	m_actions["FormatBold"]->setCheckable(true);
	m_actions["FormatItalic"] = format_menu->addAction(QIcon::fromTheme("format-text-italic"), tr("&Italic"), m_documents, SLOT(setFontItalic(bool)), QKeySequence::Italic);
	m_actions["FormatItalic"]->setCheckable(true);
	m_actions["FormatUnderline"] = format_menu->addAction(QIcon::fromTheme("format-text-underline"), tr("&Underline"), m_documents, SLOT(setFontUnderline(bool)), QKeySequence::Underline);
	m_actions["FormatUnderline"]->setCheckable(true);
	m_actions["FormatStrikeOut"] = format_menu->addAction(QIcon::fromTheme("format-text-strikethrough"), tr("Stri&kethrough"), m_documents, SLOT(setFontStrikeOut(bool)), tr("Ctrl+K"));
	m_actions["FormatStrikeOut"]->setCheckable(true);
	format_menu->addSeparator();
	m_actions["FormatSuperScript"] = format_menu->addAction(QIcon::fromTheme("format-text-superscript"), tr("Sup&erscript"), m_documents, SLOT(setFontSuperScript(bool)), tr("Ctrl+^"));
	m_actions["FormatSuperScript"]->setCheckable(true);
	m_actions["FormatSubScript"] = format_menu->addAction(QIcon::fromTheme("format-text-subscript"), tr("&Subscript"), m_documents, SLOT(setFontSubScript(bool)), tr("Ctrl+_"));
	m_actions["FormatSubScript"]->setCheckable(true);
	format_menu->addSeparator();
	m_actions["FormatAlignLeft"] = format_menu->addAction(QIcon::fromTheme("format-justify-left"), tr("Align &Left"), m_documents, SLOT(alignLeft()), tr("Ctrl+{"));
	m_actions["FormatAlignLeft"]->setCheckable(true);
	m_actions["FormatAlignCenter"] = format_menu->addAction(QIcon::fromTheme("format-justify-center"), tr("Align &Center"), m_documents, SLOT(alignCenter()), tr("Ctrl+|"));
	m_actions["FormatAlignCenter"]->setCheckable(true);
	m_actions["FormatAlignRight"] = format_menu->addAction(QIcon::fromTheme("format-justify-right"), tr("Align &Right"), m_documents, SLOT(alignRight()), tr("Ctrl+}"));
	m_actions["FormatAlignRight"]->setCheckable(true);
	m_actions["FormatAlignJustify"] = format_menu->addAction(QIcon::fromTheme("format-justify-fill"), tr("&Align Block"), m_documents, SLOT(alignJustify()), tr("Ctrl+J"));
	m_actions["FormatAlignJustify"]->setCheckable(true);
	QActionGroup* alignment = new QActionGroup(this);
	alignment->addAction(m_actions["FormatAlignLeft"]);
	alignment->addAction(m_actions["FormatAlignCenter"]);
	alignment->addAction(m_actions["FormatAlignRight"]);
	alignment->addAction(m_actions["FormatAlignJustify"]);
	m_actions["FormatAlignLeft"]->setChecked(true);
	format_menu->addSeparator();
	m_actions["FormatIndentDecrease"] = format_menu->addAction(QIcon::fromTheme("format-indent-less"), tr("&Decrease Indent"), m_documents, SLOT(decreaseIndent()), tr("Ctrl+<"));
	m_actions["FormatIndentIncrease"] = format_menu->addAction(QIcon::fromTheme("format-indent-more"), tr("I&ncrease Indent"), m_documents, SLOT(increaseIndent()), tr("Ctrl+>"));
	format_menu->addSeparator();
	m_plaintext_action = format_menu->addAction(tr("&Make Plain Text"), m_documents, SLOT(makePlainText()), tr("Ctrl+Shift+T"));
	m_richtext_action = format_menu->addAction(tr("&Make Rich Text"), m_documents, SLOT(makeRichText()), tr("Ctrl+Shift+T"));
	m_richtext_action->setVisible(false);

	// Create tools menu
	QMenu* tools_menu = menuBar()->addMenu(tr("&Tools"));
	m_actions["Find"] = tools_menu->addAction(QIcon::fromTheme("edit-find"), tr("&Find..."), m_documents, SLOT(find()), QKeySequence::Find);
	m_actions["FindNext"] = tools_menu->addAction(QIcon::fromTheme("go-down"), tr("Find &Next"), m_documents, SLOT(findNext()), QKeySequence::FindNext);
	m_actions["FindNext"]->setEnabled(false);
	connect(m_documents, SIGNAL(findNextAvailable(bool)), m_actions["FindNext"], SLOT(setEnabled(bool)));
	m_actions["FindPrevious"] = tools_menu->addAction(QIcon::fromTheme("go-up"), tr("Find Pre&vious"), m_documents, SLOT(findPrevious()), QKeySequence::FindPrevious);
	m_actions["FindPrevious"]->setEnabled(false);
	connect(m_documents, SIGNAL(findNextAvailable(bool)), m_actions["FindPrevious"], SLOT(setEnabled(bool)));
	m_actions["Replace"] = tools_menu->addAction(QIcon::fromTheme("edit-find-replace"), tr("&Replace..."), m_documents, SLOT(replace()), keyBinding(QKeySequence::Replace, tr("Ctrl+R")));
	tools_menu->addSeparator();
	m_actions["CheckSpelling"] = tools_menu->addAction(QIcon::fromTheme("tools-check-spelling"), tr("&Spelling..."), m_documents, SLOT(checkSpelling()), tr("F7"));
	m_actions["Timers"] = tools_menu->addAction(QIcon::fromTheme("chronometer"), tr("&Timers..."), m_timers, SLOT(show()));

	// Create settings menu
	QMenu* settings_menu = menuBar()->addMenu(tr("&Settings"));
	QAction* action = settings_menu->addAction(tr("Show &Toolbar"), this, SLOT(toggleToolbar(bool)));
	action->setCheckable(true);
	action->setChecked(QSettings().value("Toolbar/Shown", true).toBool());
	settings_menu->addSeparator();
	m_actions["Fullscreen"] = settings_menu->addAction(QIcon::fromTheme("view-fullscreen"), tr("&Fullscreen"), this, SLOT(toggleFullscreen()), tr("F11"));
#ifdef Q_OS_MAC
	m_actions["Fullscreen"]->setShortcut(tr("Esc"));
#endif
	m_actions["Fullscreen"]->setCheckable(true);
	settings_menu->addSeparator();
	m_actions["Themes"] = settings_menu->addAction(QIcon::fromTheme("applications-graphics"), tr("&Themes..."), this, SLOT(themeClicked()));
	m_actions["Preferences"] = settings_menu->addAction(QIcon::fromTheme("configure"), tr("&Preferences..."), this, SLOT(preferencesClicked()), QKeySequence::Preferences);

	// Create help menu
	QMenu* help_menu = menuBar()->addMenu(tr("&Help"));
	m_actions["About"] = help_menu->addAction(QIcon::fromTheme("help-about"), tr("&About"), this, SLOT(aboutClicked()));
	m_actions["AboutQt"] = help_menu->addAction(QIcon(":/trolltech/qmessagebox/images/qtlogo-64.png"), tr("About &Qt"), qApp, SLOT(aboutQt()));

	// Enable toolbar management in preferences dialog
	QHashIterator<QString, QAction*> i(m_actions);
	while (i.hasNext()) {
		i.next();
		i.value()->setData(i.key());

		// Add to format actions
		if (i.key().startsWith("Format")) {
			m_format_actions.append(i.value());
		}
	}
	addActions(m_actions.values());
}

/*****************************************************************************/
