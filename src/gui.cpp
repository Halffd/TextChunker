#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtGui/QKeyEvent>
#include <QtCore/QString>
#include <QtGui/QFont>
#include <QtGui/QScreen>
#include <fstream>
#include <string>
#include <algorithm>
#include <iostream>
#include <ctime>

class TextChunkerWindow : public QMainWindow {
    Q_OBJECT

private:
    std::string text;
    size_t chunk_size;
    bool tail_mode;
    bool inverted;
    int current_chunk;
    int total_chunks;

    QLabel* chunkLabel;
    QLabel* infoLabel;
    QLabel* helpLabel;
    QSpinBox* chunkSizeSpinBox;
    QPushButton* applyButton;
    QScrollArea* scrollArea;
    QClipboard* clipboard;

    void recalcChunks() {
        total_chunks = (text.length() + chunk_size - 1) / chunk_size;
        if (total_chunks == 0) total_chunks = 1;
        if (current_chunk > total_chunks) current_chunk = total_chunks;
        if (current_chunk < 1) current_chunk = 1;
    }

    std::string getChunk(int pos) {
        if (pos < 1 || pos > total_chunks) return "";
        size_t start_pos, end_pos;
        if (tail_mode ^ inverted) {
            end_pos = text.length() - (total_chunks - pos) * chunk_size;
            start_pos = (end_pos > chunk_size) ? end_pos - chunk_size : 0;
        } else {
            start_pos = (pos - 1) * chunk_size;
            end_pos = std::min(start_pos + chunk_size, text.length());
        }
        return text.substr(start_pos, end_pos - start_pos);
    }

    void updateUI() {
        std::string chunk = getChunk(current_chunk);
        chunkLabel->setText(QString::fromStdString(chunk));

        QString info = QString("Chunk %1/%2 | %3 total chars | %4 chars per chunk")
                           .arg(current_chunk)
                           .arg(total_chunks)
                           .arg(text.length())
                           .arg(chunk_size);
        
        QString modes = "";
        if (tail_mode) modes += "TAIL ";
        if (inverted) modes += "INVERTED ";
        if (!modes.isEmpty()) info += " | " + modes.trimmed();
        
        infoLabel->setText(info);

        // Update status bar
        statusBar()->showMessage(QString("Copied %1 characters to clipboard").arg(chunk.length()));

        // Copy to clipboard automatically
        clipboard->setText(QString::fromStdString(chunk));
    }

    void loadNewText() {
        std::string newText = clipboard->text().toStdString();
        if (newText.empty()) {
            statusBar()->showMessage("No text in clipboard!", 3000);
            return;
        }
        
        text = newText;
        current_chunk = 1;
        if (tail_mode) {
            recalcChunks();
            current_chunk = total_chunks;
        } else {
            recalcChunks();
        }
        
        statusBar()->showMessage("Loaded new text from clipboard!", 2000);
        updateUI();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        switch (event->key()) {
        case Qt::Key_N: // next
        case Qt::Key_Right:
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (tail_mode ^ inverted)
                current_chunk = std::max(1, current_chunk - 1);
            else
                current_chunk = std::min(total_chunks, current_chunk + 1);
            updateUI();
            break;
        case Qt::Key_P: // prev
        case Qt::Key_Left:
        case Qt::Key_Backspace:
            if (tail_mode ^ inverted)
                current_chunk = std::min(total_chunks, current_chunk + 1);
            else
                current_chunk = std::max(1, current_chunk - 1);
            updateUI();
            break;
        case Qt::Key_R: // recopy
        case Qt::Key_C: // also recopy
            clipboard->setText(QString::fromStdString(getChunk(current_chunk)));
            statusBar()->showMessage("Recopied to clipboard", 2000);
            break;
        case Qt::Key_I: // invert
            inverted = !inverted;
            current_chunk = total_chunks - current_chunk + 1;
            updateUI();
            break;
        case Qt::Key_F: // first
        case Qt::Key_Home:
            current_chunk = (tail_mode ^ inverted) ? total_chunks : 1;
            updateUI();
            break;
        case Qt::Key_L: // last
        case Qt::Key_End:
            current_chunk = (tail_mode ^ inverted) ? 1 : total_chunks;
            updateUI();
            break;
        case Qt::Key_V: // load new text from clipboard
            loadNewText();
            break;
        case Qt::Key_Q: // quit
        case Qt::Key_Escape:
            QApplication::quit();
            break;
        }
    }

private slots:
    void onChunkSizeChanged() {
        chunk_size = chunkSizeSpinBox->value();
        recalcChunks();
        updateUI();
    }

public:
    TextChunkerWindow(const std::string& inputText, size_t size, bool tail)
        : text(inputText), chunk_size(size), tail_mode(tail), inverted(false), current_chunk(1) {

        recalcChunks();
        if (tail_mode) current_chunk = total_chunks;

        setupUI();
        updateUI();
    }

private:
    void setupUI() {
        // Create central widget and main layout
        QWidget* central = new QWidget(this);
        QVBoxLayout* mainLayout = new QVBoxLayout(central);
        mainLayout->setSpacing(15);
        mainLayout->setContentsMargins(20, 20, 20, 20);

        // Controls section
        QHBoxLayout* controlsLayout = new QHBoxLayout();
        QLabel* chunkSizeLabel = new QLabel("Chunk Size:");
        QFont controlFont = chunkSizeLabel->font();
        controlFont.setPointSize(14);
        controlFont.setBold(true);
        chunkSizeLabel->setFont(controlFont);
        controlsLayout->addWidget(chunkSizeLabel);
        
        chunkSizeSpinBox = new QSpinBox(this);
        chunkSizeSpinBox->setRange(100, 100000);
        chunkSizeSpinBox->setValue(chunk_size);
        chunkSizeSpinBox->setSingleStep(1000);
        QFont spinBoxFont = chunkSizeSpinBox->font();
        spinBoxFont.setPointSize(14);
        chunkSizeSpinBox->setFont(spinBoxFont);
        connect(chunkSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &TextChunkerWindow::onChunkSizeChanged);
        controlsLayout->addWidget(chunkSizeSpinBox);
        
        controlsLayout->addStretch();
        mainLayout->addLayout(controlsLayout);

        // Text display area with scroll
        scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        chunkLabel = new QLabel();
        chunkLabel->setWordWrap(true);
        chunkLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        chunkLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        chunkLabel->setMargin(15);
        
        QFont chunkFont("Consolas", 12);
        if (!chunkFont.exactMatch()) {
            chunkFont.setFamily("Monaco");  // macOS fallback
        }
        if (!chunkFont.exactMatch()) {
            chunkFont.setFamily("Courier New");  // Windows fallback
        }
        chunkLabel->setFont(chunkFont);

        scrollArea->setWidget(chunkLabel);
        mainLayout->addWidget(scrollArea, 1);  // Give it all available space

        // Info section - MUCH BIGGER FONT
        infoLabel = new QLabel(this);
        infoLabel->setAlignment(Qt::AlignCenter);
        infoLabel->setWordWrap(true);
        QFont infoFont = infoLabel->font();
        infoFont.setPointSize(16);  // Bigger font
        infoFont.setBold(true);
        infoLabel->setFont(infoFont);
        mainLayout->addWidget(infoLabel);

        // Help section - MUCH BIGGER FONT
        helpLabel = new QLabel("⌨️  N/Space/Enter/→=Next  P/Backspace/←=Prev  R/C=Recopy  I=Invert  F/Home=First  L/End=Last  V=New Text  Q/Esc=Quit", this);
        helpLabel->setAlignment(Qt::AlignCenter);
        helpLabel->setWordWrap(true);
        QFont helpFont = helpLabel->font();
        helpFont.setPointSize(14);  // Much bigger font
        helpFont.setBold(true);
        helpLabel->setFont(helpFont);
        mainLayout->addWidget(helpLabel);

        setCentralWidget(central);

        // Window setup
        resize(1200, 800);
        setMinimumSize(800, 600);
        setWindowTitle("Text Chunker Pro 📝");

        // Dark theme styling
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
            QSpinBox {
                background-color: #2d2d2d;
                border: 2px solid #404040;
                border-radius: 6px;
                padding: 8px;
                color: #ffffff;
                font-size: 14px;
                min-width: 120px;
            }
            QSpinBox:focus {
                border-color: #0078d4;
            }
            QStatusBar {
                background-color: #2d2d2d;
                color: #ffffff;
                border-top: 1px solid #404040;
                font-size: 12px;
            }
        )");

        // Set object names for styling
        chunkLabel->setObjectName("chunkLabel");

        // Setup status bar
        statusBar()->setSizeGripEnabled(true);
        statusBar()->showMessage("Ready - Press V to load new text from clipboard");

        clipboard = QApplication::clipboard();
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    bool tail_mode = false;
    size_t chunk_size = 20000;
    std::string filename;

    if (argc > 1) tail_mode = (std::string(argv[1]) == "1");
    if (argc > 2) {
        chunk_size = std::stoul(argv[2]);
        if (chunk_size == 0) {
            std::cerr << "Error: Chunk size must be > 0" << std::endl;
            return 1;
        }
    }
    if (argc > 3) filename = argv[3];

    std::string inputText;
    if (!filename.empty()) {
        std::ifstream file(filename);
        if (!file) {
            std::cerr << "Error: Could not open file " << filename << std::endl;
            return 1;
        }
        inputText.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    } else {
        inputText = QApplication::clipboard()->text().toStdString();
    }

    if (inputText.empty()) {
        std::cerr << "Error: No text loaded" << std::endl;
        return 1;
    }

    TextChunkerWindow window(inputText, chunk_size, tail_mode);
    window.show();
    
    // Center the window
    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    int x = (screenGeometry.width() - window.width()) / 2;
    int y = (screenGeometry.height() - window.height()) / 2;
    window.move(x, y);
    
    return app.exec();
}

#include "gui.moc"