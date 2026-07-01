#include <QShortcut>
#include <QtCore/QDir>
#include <QtCore/QOverload>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QFont>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unistd.h>
#include <vector>
// Configuration structure
struct Config {
  size_t chunk_size = 20000;
  bool tail_mode = false;
  bool auto_exit = false;
  bool tray_mode = false;
  bool gui_hidden = false;
  double opacity = 1.0;
  int gui_width = 8600;
  int gui_height = 400;
  int gui_x = -1;    // -1 means center
  int gui_y = -1;    // -1 means center
  int gui_monitor = 0; // 0 is primary monitor
  std::vector<std::string> regex_patterns;
  bool filter_mode = false;
  std::string output_directory;
};

// Search dialog
class SearchDialog : public QDialog {
  Q_OBJECT
public:
  SearchDialog(QWidget *parent = nullptr) : QDialog(parent) {
    setWindowTitle("Search Chunks");
    setModal(true);
    setMinimumWidth(400);

    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *searchLabel = new QLabel("Search term:");
    searchInput = new QLineEdit();
    searchInput->setPlaceholderText("Enter text or regex pattern...");
    layout->addWidget(searchLabel);
    layout->addWidget(searchInput);

    useRegexCheckbox = new QCheckBox("Use Regular Expression");
    layout->addWidget(useRegexCheckbox);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *searchBtn = new QPushButton("Search");
    QPushButton *closeBtn = new QPushButton("Close");
    buttonLayout->addWidget(searchBtn);
    buttonLayout->addWidget(closeBtn);
    layout->addLayout(buttonLayout);

    connect(searchBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
  }

  QString getSearchTerm() const { return searchInput->text(); }
  bool isRegex() const { return useRegexCheckbox->isChecked(); }

private:
  QLineEdit *searchInput;
  QCheckBox *useRegexCheckbox;
};

// Regex filter dialog
class RegexFilterDialog : public QDialog {
  Q_OBJECT
public:
  RegexFilterDialog(QWidget *parent = nullptr) : QDialog(parent) {
    setWindowTitle("Regex Filters");
    setModal(true);
    setMinimumSize(500, 300);

    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *infoLabel =
        new QLabel("Add regex patterns to filter chunks (show only matching):");
    layout->addWidget(infoLabel);

    patternInput = new QLineEdit();
    patternInput->setPlaceholderText("Enter regex pattern...");
    layout->addWidget(patternInput);

    patternsDisplay = new QTextEdit();
    patternsDisplay->setReadOnly(true);
    patternsDisplay->setPlaceholderText(
        "Patterns will appear here (one per line)");
    layout->addWidget(patternsDisplay);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *addBtn = new QPushButton("Add Pattern");
    QPushButton *clearBtn = new QPushButton("Clear All");
    QPushButton *okBtn = new QPushButton("OK");
    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(clearBtn);
    buttonLayout->addWidget(okBtn);
    layout->addLayout(buttonLayout);

    connect(addBtn, &QPushButton::clicked, this,
            &RegexFilterDialog::addPattern);
    connect(clearBtn, &QPushButton::clicked, this,
            &RegexFilterDialog::clearPatterns);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
  }

  std::vector<std::string> getPatterns() const {
    std::vector<std::string> patterns;
    QString text = patternsDisplay->toPlainText();
    for (const QString &line : text.split('\n')) {
      if (!line.trimmed().isEmpty()) {
        patterns.push_back(line.toStdString());
      }
    }
    return patterns;
  }

private slots:
  void addPattern() {
    QString pattern = patternInput->text().trimmed();
    if (pattern.isEmpty())
      return;

    try {
      std::regex test(pattern.toStdString());
      QString current = patternsDisplay->toPlainText();
      if (!current.isEmpty())
        current += "\n";
      patternsDisplay->setText(current + pattern);
      patternInput->clear();
    } catch (const std::regex_error &e) {
      QMessageBox::warning(this, "Invalid Regex",
                           QString("Pattern error: %1").arg(e.what()));
    }
  }

  void clearPatterns() { patternsDisplay->clear(); }

private:
  QLineEdit *patternInput;
  QTextEdit *patternsDisplay;
};

class TextChunkerWindow : public QMainWindow {
  Q_OBJECT

private:
  std::string text;
  Config config;
  bool inverted = false;
  int current_chunk = 1;
  int total_chunks = 0;
  std::vector<std::string> filtered_chunks;
  bool filter_active = false;

  QLabel *chunkLabel;
  QLabel *infoLabel;
  QLabel *helpLabel;
  QSpinBox *chunkSizeSpinBox;
  QScrollArea *scrollArea;
  QClipboard *clipboard;
  QSystemTrayIcon *trayIcon = nullptr;

  // Global shortcuts
  QShortcut *globalNextShortcut;
  QShortcut *globalPrevShortcut;
  QShortcut *globalNewTextShortcut;

  void recalcChunks() {
    total_chunks = (text.length() + config.chunk_size - 1) / config.chunk_size;
    if (total_chunks == 0)
      total_chunks = 1;
    if (current_chunk > total_chunks)
      current_chunk = total_chunks;
    if (current_chunk < 1)
      current_chunk = 1;
  }

  const std::string getChunk(int pos) const {
    if (pos < 1 || pos > total_chunks || total_chunks <= 0)
      return "";

    size_t start_pos, end_pos;
    if (config.tail_mode ^ inverted) {
      int chunk_from_end = total_chunks - pos + 1;
      end_pos = text.length() - (chunk_from_end - 1) * config.chunk_size;
      start_pos =
          (end_pos > config.chunk_size) ? end_pos - config.chunk_size : 0;

      if (end_pos > text.length())
        end_pos = text.length();
      if (start_pos >= text.length())
        return "";
    } else {
      start_pos = (pos - 1) * config.chunk_size;
      end_pos = std::min(start_pos + config.chunk_size, text.length());

      if (start_pos >= text.length())
        return "";
    }

    if (start_pos >= end_pos)
      return "";
    return text.substr(start_pos, end_pos - start_pos);
  }

  bool matchesFilters(const std::string &chunk) const {
    if (config.regex_patterns.empty())
      return true;

    for (const auto &pattern : config.regex_patterns) {
      try {
        std::regex regex(pattern);
        if (std::regex_search(chunk, regex)) {
          return true;
        }
      } catch (const std::regex_error &) {
        continue;
      }
    }
    return false;
  }

  void applyFilters() {
    filtered_chunks.clear();
    filter_active = !config.regex_patterns.empty();

    if (!filter_active) {
      total_chunks =
          (text.length() + config.chunk_size - 1) / config.chunk_size;
      if (total_chunks == 0)
        total_chunks = 1;
      current_chunk = 1;
      updateUI();
      return;
    }

    int chunk_count =
        (text.length() + config.chunk_size - 1) / config.chunk_size;
    if (chunk_count == 0)
      chunk_count = 1;

    for (int i = 1; i <= chunk_count; ++i) {
      const std::string chunk = getChunk(i);
      if (matchesFilters(chunk)) {
        filtered_chunks.push_back(chunk);
      }
    }

    total_chunks = filtered_chunks.empty() ? 1 : filtered_chunks.size();
    current_chunk = 1;
    updateUI();
  }

  std::string getDisplayChunk(int pos) const {
    if (filter_active) {
      if (pos < 1 || pos > (int)filtered_chunks.size())
        return "";
      return filtered_chunks[pos - 1];
    }
    return getChunk(pos);
  }

  void updateUI() {
    std::string chunk = getDisplayChunk(current_chunk);
    chunkLabel->setText(QString::fromStdString(chunk));

    QString info = QString("Chunk %1/%2 | %3 total chars")
                       .arg(current_chunk)
                       .arg(total_chunks)
                       .arg(text.length());

    if (filter_active) {
      info += QString(" | FILTERED: %1 matches").arg(total_chunks);
    }

    QString modes = "";
    if (config.tail_mode)
      modes += "TAIL ";
    if (inverted)
      modes += "INVERTED ";
    if (filter_active)
      modes += "FILTER ";
    if (!modes.isEmpty())
      info += " | " + modes.trimmed();

    infoLabel->setText(info);

    clipboard->setText(QString::fromStdString(chunk));
    statusBar()->showMessage(QString("Chunk %1/%2 • %3 characters")
                                 .arg(current_chunk)
                                 .arg(total_chunks)
                                 .arg(chunk.length()));
  }

  void goNext() {
    if (total_chunks <= 0)
      return;

    if (config.tail_mode ^ inverted) {
      current_chunk = std::max(1, current_chunk - 1);
    } else {
      current_chunk = std::min(total_chunks, current_chunk + 1);
    }
    updateUI();
  }

  void goPrev() {
    if (total_chunks <= 0)
      return;

    if (config.tail_mode ^ inverted) {
      current_chunk = std::min(total_chunks, current_chunk + 1);
    } else {
      current_chunk = std::max(1, current_chunk - 1);
    }
    updateUI();
  }

  void loadNewText() {
    std::string newText = clipboard->text().toStdString();
    if (newText.empty()) {
      statusBar()->showMessage("No text in clipboard!", 3000);
      return;
    }

    text = newText;
    current_chunk = 1;
    recalcChunks();

    if (config.tail_mode) {
      current_chunk = total_chunks;
    }

    applyFilters();
    statusBar()->showMessage("Loaded new text from clipboard!", 2000);
  }

  void appendText() {
    std::string newText = clipboard->text().toStdString();
    if (newText.empty()) {
      statusBar()->showMessage("No text in clipboard!", 3000);
      return;
    }

    text += newText;
    recalcChunks();
    applyFilters();
    statusBar()->showMessage("Appended text from clipboard!", 2000);
  }

  void replaceText() {
    std::string newText = clipboard->text().toStdString();
    if (newText.empty()) {
      statusBar()->showMessage("No text in clipboard!", 3000);
      return;
    }

    text = newText;
    current_chunk = 1;
    recalcChunks();
    applyFilters();
    statusBar()->showMessage("Replaced text from clipboard!", 2000);
  }

  void saveChunksToDirectory() {
    QString dir =
        QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (dir.isEmpty())
      return;

    try {
      int saved = 0;
      int max_chunks = filter_active ? filtered_chunks.size() : total_chunks;

      for (int i = 1; i <= max_chunks; ++i) {
        std::string chunk = getDisplayChunk(i);
        if (chunk.empty())
          continue;

        QString filename =
            QString("%1/chunk_%2.txt").arg(dir).arg(i, 6, 10, QChar('0'));
        std::ofstream file(filename.toStdString());
        if (file) {
          file << chunk;
          file.close();
          saved++;
        }
      }

      statusBar()->showMessage(
          QString("Saved %1 chunks to %2").arg(saved).arg(dir), 3000);
    } catch (const std::exception &e) {
      QMessageBox::critical(this, "Error",
                            QString("Failed to save chunks: %1").arg(e.what()));
    }
  }

  void searchChunks() {
    SearchDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
      return;

    QString searchTerm = dialog.getSearchTerm();
    if (searchTerm.isEmpty())
      return;

    int matches = 0;
    for (int i = 1; i <= total_chunks; ++i) {
      std::string chunk = getDisplayChunk(i);

      bool found = false;
      if (dialog.isRegex()) {
        try {
          std::regex regex(searchTerm.toStdString());
          found = std::regex_search(chunk, regex);
        } catch (const std::regex_error &) {
          QMessageBox::warning(this, "Invalid Regex", "Invalid regex pattern");
          return;
        }
      } else {
        found = chunk.find(searchTerm.toStdString()) != std::string::npos;
      }

      if (found) {
        current_chunk = i;
        matches++;
        break;
      }
    }

    if (matches > 0) {
      updateUI();
      statusBar()->showMessage(
          QString("Found match in chunk %1").arg(current_chunk), 2000);
    } else {
      statusBar()->showMessage("No matches found", 2000);
    }
  }

  void setupRegexFilters() {
    RegexFilterDialog dialog(this);
    dialog.show();

    if (dialog.exec() == QDialog::Accepted) {
      config.regex_patterns = dialog.getPatterns();
      applyFilters();
      statusBar()->showMessage(
          QString("Applied %1 filter(s)").arg(config.regex_patterns.size()),
          2000);
    }
  }

  void setupTrayIcon() {
    if (!trayIcon) {
      trayIcon = new QSystemTrayIcon(this);

      QMenu *trayMenu = new QMenu(this);
      trayMenu->addAction("Show", this, &QWidget::showNormal);
      trayMenu->addAction("Hide", this, &QWidget::hide);
      trayMenu->addSeparator();
      trayMenu->addAction("Next (Ctrl+Shift+V)", this,
                          &TextChunkerWindow::goNext);
      trayMenu->addAction("Previous (Ctrl+Shift+P)", this,
                          &TextChunkerWindow::goPrev);
      trayMenu->addSeparator();
      trayMenu->addAction("Quit", qApp, &QApplication::quit);

      trayIcon->setContextMenu(trayMenu);
      trayIcon->setIcon(QIcon::fromTheme("document-properties"));
      trayIcon->show();

      connect(trayIcon, &QSystemTrayIcon::activated, this,
              [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                  isVisible() ? hide() : showNormal();
                }
              });
    }
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->isAutoRepeat())
      return;

    switch (event->key()) {
    case Qt::Key_N:
    case Qt::Key_Right:
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
      goNext();
      break;
    case Qt::Key_P:
    case Qt::Key_Left:
    case Qt::Key_Backspace:
      goPrev();
      break;
    case Qt::Key_R:
    case Qt::Key_C:
      clipboard->setText(
          QString::fromStdString(getDisplayChunk(current_chunk)));
      statusBar()->showMessage("Recopied to clipboard", 2000);
      break;
    case Qt::Key_I:
      inverted = !inverted;
      if (total_chunks > 0) {
        current_chunk = total_chunks - current_chunk + 1;
        current_chunk = std::max(1, std::min(current_chunk, total_chunks));
      }
      updateUI();
      break;
    case Qt::Key_F:
    case Qt::Key_Home:
      current_chunk = (config.tail_mode ^ inverted) ? total_chunks : 1;
      updateUI();
      break;
    case Qt::Key_L:
    case Qt::Key_End:
      current_chunk = (config.tail_mode ^ inverted) ? 1 : total_chunks;
      updateUI();
      break;
    case Qt::Key_V:
      loadNewText();
      break;
    case Qt::Key_A:
      appendText();
      break;
    case Qt::Key_T:
      replaceText();
      break;
    case Qt::Key_S:
      saveChunksToDirectory();
      break;
    case Qt::Key_F3:
      searchChunks();
      break;
    case Qt::Key_F4:
      setupRegexFilters();
      break;
    case Qt::Key_Q:
    case Qt::Key_Escape:
      if (config.auto_exit) {
        QApplication::quit();
      } else {
        if (config.tray_mode) {
          hide();
        } else {
          QApplication::quit();
        }
      }
      break;
    }
  }

  void closeEvent(QCloseEvent *event) override {
    if (config.tray_mode && trayIcon && trayIcon->isVisible()) {
      hide();
      event->ignore();
    } else {
      event->accept();
    }
  }

private slots:
  void onChunkSizeChanged() {
    config.chunk_size = chunkSizeSpinBox->value();
    recalcChunks();
    applyFilters();
  }

public:
  TextChunkerWindow(const std::string &inputText, const Config &cfg)
      : text(inputText), config(cfg), current_chunk(1) {

    setupUI();

    recalcChunks();
    if (config.tail_mode)
      current_chunk = total_chunks;

    setupGlobalShortcuts();

    if (config.tray_mode) {
      setupTrayIcon();
    }

    applyFilters();
    updateUI();

    if (config.gui_hidden) {
      hide();
    }
  }

private:
  void setupGlobalShortcuts() {
    globalNextShortcut = new QShortcut(QKeySequence("Ctrl+Shift+V"), this);
    globalNextShortcut->setContext(Qt::ApplicationShortcut);
    connect(globalNextShortcut, &QShortcut::activated, this,
            &TextChunkerWindow::goNext);

    globalPrevShortcut = new QShortcut(QKeySequence("Ctrl+Shift+P"), this);
    globalPrevShortcut->setContext(Qt::ApplicationShortcut);
    connect(globalPrevShortcut, &QShortcut::activated, this,
            &TextChunkerWindow::goPrev);

#ifdef Q_OS_LINUX
    globalNewTextShortcut = new QShortcut(QKeySequence("Ctrl+Meta+V"), this);
#elif defined(Q_OS_MAC)
    globalNewTextShortcut = new QShortcut(QKeySequence("Ctrl+Cmd+V"), this);
#else
    globalNewTextShortcut = new QShortcut(QKeySequence("Ctrl+Win+V"), this);
#endif
    globalNewTextShortcut->setContext(Qt::ApplicationShortcut);
    connect(globalNewTextShortcut, &QShortcut::activated, this,
            &TextChunkerWindow::loadNewText);

    statusBar()->showMessage("Global hotkeys: Ctrl+Shift+V=Next, "
                             "Ctrl+Shift+P=Prev, Ctrl+Super+V=New Text",
                             5000);
  }

  void setupUI() {
    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Controls section
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    QLabel *chunkSizeLabel = new QLabel("Chunk Size:");
    QFont controlFont = chunkSizeLabel->font();
    controlFont.setPointSize(14);
    controlFont.setBold(true);
    chunkSizeLabel->setFont(controlFont);
    controlsLayout->addWidget(chunkSizeLabel);

    chunkSizeSpinBox = new QSpinBox(this);
    chunkSizeSpinBox->setRange(100, 100000);
    chunkSizeSpinBox->setValue(config.chunk_size);
    chunkSizeSpinBox->setSingleStep(1000);
    QFont spinBoxFont = chunkSizeSpinBox->font();
    spinBoxFont.setPointSize(14);
    chunkSizeSpinBox->setFont(spinBoxFont);
    connect(chunkSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &TextChunkerWindow::onChunkSizeChanged);
    controlsLayout->addWidget(chunkSizeSpinBox);

    QPushButton *filterBtn = new QPushButton("Set Regex Filters (F4)");
    filterBtn->setMaximumWidth(200);
    connect(filterBtn, &QPushButton::clicked, this,
            &TextChunkerWindow::setupRegexFilters);
    controlsLayout->addWidget(filterBtn);

    QPushButton *searchBtn = new QPushButton("Search (F3)");
    searchBtn->setMaximumWidth(120);
    connect(searchBtn, &QPushButton::clicked, this,
            &TextChunkerWindow::searchChunks);
    controlsLayout->addWidget(searchBtn);

    QPushButton *saveBtn = new QPushButton("Save to Dir (S)");
    saveBtn->setMaximumWidth(140);
    connect(saveBtn, &QPushButton::clicked, this,
            &TextChunkerWindow::saveChunksToDirectory);
    controlsLayout->addWidget(saveBtn);

    controlsLayout->addStretch();
    mainLayout->addLayout(controlsLayout);

    // Text display area
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    chunkLabel = new QLabel();
    chunkLabel->setObjectName("chunkLabel");
    chunkLabel->setWordWrap(true);
    chunkLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    chunkLabel->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                        Qt::TextSelectableByKeyboard);
    chunkLabel->setMargin(15);

    QFont chunkFont;
    QStringList fontFamilies = {"Consolas", "Monaco", "Courier New", "Courier",
                                "monospace"};
    for (const QString &family : fontFamilies) {
      chunkFont.setFamily(family);
      if (QFont(family).exactMatch() || family == "monospace")
        break;
    }
    chunkFont.setPointSize(12);
    chunkLabel->setFont(chunkFont);

    scrollArea->setWidget(chunkLabel);
    mainLayout->addWidget(scrollArea, 1);

    // Info section
    infoLabel = new QLabel(this);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setWordWrap(true);
    QFont infoFont = infoLabel->font();
    infoFont.setPointSize(16);
    infoFont.setBold(true);
    infoLabel->setFont(infoFont);
    mainLayout->addWidget(infoLabel);

    // Help section
    helpLabel =
        new QLabel("⌨️  Local: N/→=Next  P/←=Prev  R/C=Recopy  V=Load  A=Append "
                   " T=Replace  Q/Esc=Quit\n"
                   "🔍 Search: F3=Find  F4=Regex Filters  S=Save  I=Invert",
                   this);
    helpLabel->setAlignment(Qt::AlignCenter);
    helpLabel->setWordWrap(true);
    QFont helpFont = helpLabel->font();
    helpFont.setPointSize(12);
    helpFont.setBold(true);
    helpLabel->setFont(helpFont);
    mainLayout->addWidget(helpLabel);

    setCentralWidget(central);

    resize(config.gui_width, config.gui_height);
    setMinimumSize(800, 600);
    setWindowTitle("Text Chunker Pro 📝 [Piping + Filters + Tray]");
    setWindowOpacity(config.opacity);

    setStyleSheet(R"(
            QMainWindow {
                background-color: #1e1e1e;
                color: #ffffff;
            }
            QLabel {
                color: #ffffff;
                background-color: transparent;
            }
            QLabel#chunkLabel {
                background-color: #2d2d2d;
                border: 2px solid #404040;
                border-radius: 8px;
                padding: 15px;
                selection-background-color: #0078d4;
            }
            QScrollArea {
                background-color: #2d2d2d;
                border: 2px solid #404040;
                border-radius: 8px;
            }
            QScrollBar:vertical {
                background: #404040;
                width: 12px;
                border-radius: 6px;
            }
            QScrollBar::handle:vertical {
                background: #606060;
                border-radius: 6px;
                min-height: 20px;
            }
            QScrollBar::handle:vertical:hover {
                background: #707070;
            }
            QSpinBox, QPushButton, QLineEdit, QComboBox, QCheckBox, QDoubleSpinBox {
                background-color: #2d2d2d;
                border: 2px solid #404040;
                border-radius: 6px;
                padding: 8px;
                color: #ffffff;
                font-size: 12px;
            }
            QSpinBox:focus, QPushButton:focus, QLineEdit:focus {
                border-color: #0078d4;
            }
            QPushButton:hover {
                background-color: #3d3d3d;
            }
            QPushButton:pressed {
                background-color: #0078d4;
            }
            QStatusBar {
                background-color: #2d2d2d;
                color: #ffffff;
                border-top: 1px solid #404040;
                font-size: 12px;
            }
            QDialog {
                background-color: #1e1e1e;
                color: #ffffff;
            }
            QTextEdit, QTextBrowser {
                background-color: #2d2d2d;
                border: 2px solid #404040;
                border-radius: 6px;
                color: #ffffff;
            }
        )");

    statusBar()->setSizeGripEnabled(true);
    statusBar()->showMessage("Ready - Piping & Filtering Enabled!");

    clipboard = QApplication::clipboard();
  }
};

std::string readStdin() {
  std::string result;
  std::string line;
  while (std::getline(std::cin, line)) {
    result += line + "\n";
  }
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }
  return result;
}

void printHelp() {
  std::cout << R"(
Text Chunker Pro - Enhanced Edition
Usage: text_chunker [OPTIONS]

OPTIONS:
  --tail, -t              Start from end (tail mode)
  --chunk-size, -c N      Set chunk size (default: 20000)
  --file, -f PATH         Load file (default: stdin/clipboard)
  --auto-exit, -x         Exit after clipboard operation
  --tray, -y              Minimize to system tray
  --hidden, -d            Start with GUI hidden
  --width W               Set window width (default: 1200)
  --height H              Set window height (default: 850)
  --x X                   Set window X position (default: centered)
  --y Y                   Set window Y position (default: centered)
  --monitor M             Set monitor index (default: 0)
  --opacity O             Set window opacity 0.0-1.0 (default: 1.0)
  --regex, -r PATTERN     Add regex filter pattern (can use multiple)
  --output, -o DIR        Set output directory for saving chunks
  --help, -h              Show this help message

KEYBOARD SHORTCUTS:
  Navigation:
    N / Space / Enter / → = Next chunk
    P / Backspace / ←     = Previous chunk
    Home / F              = First chunk
    End / L               = Last chunk
    I                     = Invert chunk order

  Text Operations:
    V                     = Load new text from clipboard
    A                     = Append text from clipboard
    T                     = Replace text from clipboard
    R / C                 = Recopy current chunk
    S                     = Save all chunks to directory
    Q / Esc               = Quit (or hide if tray mode)

  Filtering & Search:
    F3                    = Search chunks
    F4                    = Configure regex filters

  Global Hotkeys (work from any window):
    Ctrl+Shift+V          = Next chunk
    Ctrl+Shift+P          = Previous chunk
    Ctrl+Super+V          = Load new text (Ctrl+Cmd+V on Mac, Ctrl+Meta+V on Linux)

EXAMPLES:
  # Read from stdin with 10000 char chunks
  echo "Long text" | text_chunker --chunk-size 10000

  # Load file in tail mode
  text_chunker --tail --file document.txt

  # Start hidden in tray with regex filter
  text_chunker --hidden --tray --regex "Error|Warning"

  # Set custom opacity and size
  text_chunker --opacity 0.8 --width 1400 --height 900

  # Save chunks to directory
  text_chunker --output /tmp/chunks --regex "^[A-Z]"
)" << std::endl;
}

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  Config config;
  std::string filename;
  bool read_stdin = false;

  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      printHelp();
      return 0;
    } else if (arg == "--tail" || arg == "-t") {
      config.tail_mode = true;
    } else if (arg == "--auto-exit" || arg == "-x") {
      config.auto_exit = true;
    } else if (arg == "--tray" || arg == "-y") {
      config.tray_mode = true;
    } else if (arg == "--hidden" || arg == "-d") {
      config.gui_hidden = true;
    } else if (arg == "--chunk-size" || arg == "-c") {
      if (i + 1 < argc) {
        try {
          config.chunk_size = std::stoul(argv[++i]);
          if (config.chunk_size == 0) {
            std::cerr << "Error: Chunk size must be > 0" << std::endl;
            return 1;
          }
        } catch (const std::exception &e) {
          std::cerr << "Error: Invalid chunk size: " << e.what() << std::endl;
          return 1;
        }
      }
    } else if (arg == "--file" || arg == "-f") {
      if (i + 1 < argc) {
        filename = argv[++i];
      }
    } else if (arg == "--width") {
      if (i + 1 < argc) {
        try {
          config.gui_width = std::stoi(argv[++i]);
        } catch (...) {
          std::cerr << "Error: Invalid width" << std::endl;
          return 1;
        }
      }
    } else if (arg == "--height") {
      if (i + 1 < argc) {
        try {
          config.gui_height = std::stoi(argv[++i]);
        } catch (...) {
          std::cerr << "Error: Invalid height" << std::endl;
          return 1;
        }
      }
    } else if (arg == "--x") {
      if (i + 1 < argc) {
        try {
          config.gui_x = std::stoi(argv[++i]);
        } catch (...) {
          std::cerr << "Error: Invalid x position" << std::endl;
          return 1;
        }
      }
    } else if (arg == "--y") {
      if (i + 1 < argc) {
        try {
          config.gui_y = std::stoi(argv[++i]);
        } catch (...) {
          std::cerr << "Error: Invalid y position" << std::endl;
          return 1;
        }
      }
    } else if (arg == "--monitor") {
      if (i + 1 < argc) {
        try {
          config.gui_monitor = std::stoi(argv[++i]);
        } catch (...) {
          std::cerr << "Error: Invalid monitor index" << std::endl;
          return 1;
        }
      }
    } else if (arg == "--opacity" || arg == "-op") {
      if (i + 1 < argc) {
        try {
          config.opacity = std::stod(argv[++i]);
          config.opacity = std::max(0.1, std::min(1.0, config.opacity));
        } catch (...) {
          std::cerr << "Error: Invalid opacity" << std::endl;
          return 1;
        }
      }
    } else if (arg == "--regex" || arg == "-r") {
      if (i + 1 < argc) {
        std::string pattern = argv[++i];
        try {
          std::regex test(pattern);
          config.regex_patterns.push_back(pattern);
          config.filter_mode = true;
        } catch (const std::regex_error &e) {
          std::cerr << "Error: Invalid regex pattern: " << e.what()
                    << std::endl;
          return 1;
        }
      }
    } else if (arg == "--output" || arg == "-o") {
      if (i + 1 < argc) {
        config.output_directory = argv[++i];
      }
    }
  }

  // Load text
  std::string inputText;
  if (!filename.empty()) {
    std::ifstream file(filename);
    if (!file) {
      std::cerr << "Error: Could not open file " << filename << std::endl;
      return 1;
    }
    inputText.assign((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
  } else {
    // Try to read from stdin first (if piped)
    if (!isatty(fileno(stdin))) {
      inputText = readStdin();
    }

    // If no stdin data, use clipboard
    if (inputText.empty()) {
      inputText = QApplication::clipboard()->text().toStdString();
    }
  }

  if (inputText.empty()) {
    std::cerr << "Error: No text loaded (use --file, stdin, or clipboard)"
              << std::endl;
    printHelp();
    return 1;
  }

  TextChunkerWindow window(inputText, config);
  window.show();

  // Position the window
  QList<QScreen*> screens = QGuiApplication::screens();
  int monitor_index = std::max(0, std::min(config.gui_monitor, (int)screens.size() - 1));
  QScreen *screen = screens[monitor_index];
  QRect screenGeometry = screen->geometry();

  int x = (config.gui_x >= 0) ? (screenGeometry.x() + config.gui_x) : (screenGeometry.x() + (screenGeometry.width() - window.width()) / 2);
  int y = (config.gui_y >= 0) ? (screenGeometry.y() + config.gui_y) : (screenGeometry.y() + (screenGeometry.height() - window.height()) / 2);

  window.move(x, y);

  return app.exec();
}

#include "main.moc"
