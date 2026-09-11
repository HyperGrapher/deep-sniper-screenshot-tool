#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_ask.H>
#include <FL/platform.H>
#include <SDL3/SDL.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include "browser_detector.hpp"
#include "capture_engine.hpp"
#include "capture_geometry.hpp"
#include "overlay.hpp"
#include "settings.hpp"
#include "tray_icon.hpp"
#include "ui_widgets.hpp"

namespace {

constexpr wchar_t kAppId[] = L"DeepSniper";
constexpr wchar_t kSingleInstanceName[] = L"Local\\DeepSniper.SingleInstance";

using UiTheme::kBackground;
using UiTheme::kPanel;
using UiTheme::kText;
using UiTheme::kMuted;
using UiTheme::kAccent;
using UiTheme::kDanger;

class App;

[[nodiscard]] std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

[[nodiscard]] std::wstring utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

[[nodiscard]] std::wstring windowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    title.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return title;
}

[[nodiscard]] std::filesystem::path applicationDataDirectory() {
    PWSTR rawPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath))) {
        throw std::runtime_error("Unable to locate Local AppData.");
    }
    const std::filesystem::path path = std::filesystem::path{rawPath} / kAppId;
    CoTaskMemFree(rawPath);
    std::filesystem::create_directories(path);
    return path;
}

void configureLogging(const std::filesystem::path& dataDirectory) {
    const auto logDirectory = dataDirectory / L"logs";
    std::filesystem::create_directories(logDirectory);
    auto logger = spdlog::rotating_logger_mt(
        "application", wideToUtf8((logDirectory / L"app.log").wstring()), 1024 * 1024, 3);
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    spdlog::flush_on(spdlog::level::info);
}

[[nodiscard]] std::optional<std::filesystem::path> chooseFolder(HWND owner) {
    IFileOpenDialog* rawDialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rawDialog)))) {
        return std::nullopt;
    }
    std::unique_ptr<IFileOpenDialog, void (*)(IFileOpenDialog*)> dialog{rawDialog, [](IFileOpenDialog* value) { value->Release(); }};
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (dialog->Show(owner) != S_OK) {
        return std::nullopt;
    }
    IShellItem* rawItem = nullptr;
    if (FAILED(dialog->GetResult(&rawItem))) {
        return std::nullopt;
    }
    std::unique_ptr<IShellItem, void (*)(IShellItem*)> item{rawItem, [](IShellItem* value) { value->Release(); }};
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
        return std::nullopt;
    }
    std::filesystem::path result{rawPath};
    CoTaskMemFree(rawPath);
    return result;
}

Fl_Box* addLabel(int x, int y, int width, int height, const char* text, Fl_Color color = kText) {
    auto* label = new Fl_Box(x, y, width, height, text);
    label->box(FL_NO_BOX);
    label->labelcolor(color);
    label->labelsize(12);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    return label;
}

void configureDarkTitleBar(Fl_Window& window) {
    const HWND nativeWindow = fl_xid(&window);
    if (nativeWindow == nullptr) {
        return;
    }
    const BOOL useDarkTitleBar = TRUE;
    const HRESULT result = DwmSetWindowAttribute(nativeWindow, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                                 &useDarkTitleBar, sizeof(useDarkTitleBar));
    if (FAILED(result)) {
        spdlog::warn("Unable to enable dark Settings title bar: HRESULT 0x{:08X}",
                     static_cast<unsigned long>(result));
    }
}

class SettingsWindow final : public Fl_Double_Window {
public:
    using Fl_Double_Window::Fl_Double_Window;

    int handle(int event) override {
        // FLTK's Windows driver sends FL_SHOW after creating the HWND and
        // before ShowWindow. hide() destroys that HWND, so theme every new one.
        if (event == FL_SHOW) {
            configureDarkTitleBar(*this);
        }
        return Fl_Double_Window::handle(event);
    }
};

class InstanceMutex final {
public:
    InstanceMutex() {
        handle_ = CreateMutexW(nullptr, TRUE, kSingleInstanceName);
        if (handle_ == nullptr) {
            throw std::runtime_error("Unable to create the single-instance mutex.");
        }
        alreadyExists_ = GetLastError() == ERROR_ALREADY_EXISTS;
    }
    ~InstanceMutex() {
        if (handle_ != nullptr) {
            if (!alreadyExists_) {
                ReleaseMutex(handle_);
            }
            CloseHandle(handle_);
        }
    }
    [[nodiscard]] bool alreadyExists() const { return alreadyExists_; }

private:
    HANDLE handle_{};
    bool alreadyExists_{};
};

class HotkeyButton final : public ThemedButton {
public:
    HotkeyButton(int x, int y, int width, int height) : ThemedButton(x, y, width, height, "") {}

    void setHotkey(Hotkey hotkey) {
        hotkey_ = hotkey;
        const std::string name = wideToUtf8(hotkeyDisplayName(hotkey_));
        copy_label(name.c_str());
    }
    [[nodiscard]] Hotkey hotkey() const { return hotkey_; }

    int handle(int event) override {
        if (event == FL_PUSH) {
            take_focus();
            copy_label("Press Print Screen or a shortcut");
            return 1;
        }
        if (event != FL_KEYDOWN) {
            return ThemedButton::handle(event);
        }

        const int key = Fl::event_original_key();
        std::uint32_t virtualKey{};
        if (key == FL_Print) {
            virtualKey = VK_SNAPSHOT;
        } else if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')) {
            virtualKey = static_cast<std::uint32_t>(std::toupper(key));
        } else if (key >= '0' && key <= '9') {
            virtualKey = static_cast<std::uint32_t>(key);
        } else {
            return 1;
        }

        std::uint32_t modifiers{};
        const int state = Fl::event_state();
        if ((state & FL_CTRL) != 0) {
            modifiers |= MOD_CONTROL;
        }
        if ((state & FL_ALT) != 0) {
            modifiers |= MOD_ALT;
        }
        if ((state & FL_SHIFT) != 0) {
            modifiers |= MOD_SHIFT;
        }
        if ((state & FL_META) != 0) {
            modifiers |= MOD_WIN;
        }
        if (virtualKey != VK_SNAPSHOT && modifiers == 0U) {
            copy_label("Letters and digits need a modifier");
            return 1;
        }
        setHotkey(Hotkey{modifiers, virtualKey});
        return 1;
    }

private:
    Hotkey hotkey_{};
};

class ReviewWindow final : public Fl_Double_Window {
public:
    ReviewWindow(App& app, int width, int height) : Fl_Double_Window(width, height), app_(app) {}
    int handle(int event) override;

private:
    App& app_;
};

class App final {
public:
    App()
        : dataDirectory_(applicationDataDirectory()),
          settingsStore_(dataDirectory_ / L"settings.json"),
          tray_([this] { startCapture(); }, [this] { showSettings(); }, [this] { requestExit(); }) {
        configureLogging(dataDirectory_);
        try {
            settings_ = settingsStore_.load();
        } catch (const std::exception& error) {
            settings_ = defaultSettings();
            spdlog::warn("Settings could not be loaded; defaults are active: {}", error.what());
        }
        Fl::scheme("base");
        Fl::set_font(FL_HELVETICA, "Segoe UI");
        Fl::set_font(FL_HELVETICA_BOLD, "BSegoe UI");
        Fl::background(17, 19, 24);
        Fl::background2(34, 40, 50);
        Fl::foreground(235, 239, 245);
        Fl_Tooltip::color(kPanel);
        Fl_Tooltip::textcolor(kText);
        Fl_Tooltip::size(12);
        Fl_Tooltip::delay(0.35F);
        if (!tray_.create()) {
            throw std::runtime_error("Unable to create the notification-area icon.");
        }
        isHotkeyRegistered_ = tray_.registerCaptureHotkey(settings_.captureHotkey);
        if (!isHotkeyRegistered_) {
            tray_.showNotification(L"Deep Sniper hotkey unavailable", L"The configured shortcut is already in use. Capture remains available from the tray icon.");
        }
        spdlog::info("Deep Sniper started");
    }

    ~App() {
        cancelCapture();
        spdlog::info("Deep Sniper stopped");
        spdlog::shutdown();
    }

    [[nodiscard]] bool isRunning() const { return isRunning_; }
    [[nodiscard]] bool hasVisibleWindow() const {
        return (settingsWindow_ != nullptr && settingsWindow_->shown()) || (reviewWindow_ != nullptr && reviewWindow_->shown());
    }
    [[nodiscard]] bool isSelecting() const { return sessionState_.value() == CaptureState::Selecting; }

    void tick() {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {}
        if (sessionState_.value() == CaptureState::Reviewing && !destinationsShown_) {
            int x{}, y{};
            Fl::get_mouse(x, y);
            if ((x >= reviewWindow_->x() + 64 && x < reviewWindow_->x() + 112 &&
                 y >= reviewWindow_->y() + 8 && y < reviewWindow_->y() + 56) ||
                Fl::focus() == saveAsButton_) {
                showDestinations();
            }
        }
        if (sessionState_.value() != CaptureState::Selecting) {
            return;
        }
        if (inputHook_.takeCancelRequest()) {
            cancelCapture();
            return;
        }
        // Commit the target from the displayed frame before another hover/UIA
        // result can change it underneath the selection click.
        if (inputHook_.takeSelectionRequest()) {
            if (currentTarget_.has_value()) {
                captureCurrentTarget();
            }
            return;
        }
        POINT cursor{};
        GetCursorPos(&cursor);
        updateTarget(cursor);
    }

    void startCapture() {
        if (sessionState_.value() != CaptureState::Idle) {
            return;
        }
        if (settingsWindow_ != nullptr) {
            settingsWindow_->hide();
        }
        try {
            overlay_.show(primaryMonitorBounds());
            if (!inputHook_.install()) {
                overlay_.hide();
                throw std::runtime_error("Unable to monitor capture input.");
            }
            currentTarget_.reset();
            currentBrowserWindow_ = nullptr;
            currentBrowserGeneration_ = 0;
            static_cast<void>(sessionState_.startSelecting());
            spdlog::info("Capture mode started");
        } catch (const std::exception& error) {
            reportError(error.what());
        }
    }

    void cancelCapture() {
        inputHook_.remove();
        overlay_.hide();
        if (reviewWindow_ != nullptr) {
            reviewWindow_->hide();
        }
        currentTarget_.reset();
        capturedImage_.reset();
        capturedWindowTitle_.clear();
        sessionState_.reset();
    }
    void cancelReview() {
        spdlog::info("Captured image discarded");
        cancelCapture();
    }

private:
    std::filesystem::path dataDirectory_;
    SettingsStore settingsStore_;
    Settings settings_;
    TrayIcon tray_;
    CaptureOverlay overlay_;
    SelectionInputHook inputHook_;
    BrowserElementDetector browserDetector_;
    std::unique_ptr<Fl_Double_Window> settingsWindow_;
    std::unique_ptr<ReviewWindow> reviewWindow_;
    Fl_Input* folderInput_{};
    Fl_Choice* formatChoice_{};
    Fl_Input* titleLengthInput_{};
    std::array<Fl_Input*, 7> destinationInputs_{};
    std::array<ThemedButton*, 7> destinationBrowseButtons_{};
    ThemedButton* saveAsButton_{};
    Fl_Scroll* destinationRow_{};
    std::array<std::filesystem::path, 10> reviewFolders_{};
    std::array<ThemedButton*, 10> destinationButtons_{};
    bool destinationsShown_{};
    HotkeyButton* hotkeyButton_{};
    Fl_Box* hotkeyStatus_{};
    std::optional<CaptureTarget> currentTarget_;
    std::optional<CapturedImage> capturedImage_;
    std::wstring capturedWindowTitle_;
    HWND currentBrowserWindow_{};
    POINT lastBrowserPoint_{-1, -1};
    std::uint64_t currentBrowserGeneration_{};
    PixelRect lastBrowserWindowBounds_{};
    std::optional<PixelRect> currentBrowserBounds_;
    std::chrono::steady_clock::time_point browserRequestStarted_{};
    CaptureSessionState sessionState_;
    bool isRunning_{true};
    bool isHotkeyRegistered_{true};

    void updateTarget(POINT cursor) {
        const auto windowTarget = findWindowTargetAtPoint(cursor);
        if (!windowTarget.has_value()) {
            currentTarget_.reset();
            currentBrowserWindow_ = nullptr;
            overlay_.updateHighlight({}, false);
            return;
        }
        const HoverTargetRoute route = routeHoverTarget(
            isBrowserWindow(windowTarget->window),
            isPointInWindowClientArea(windowTarget->window, cursor));
        if (route == HoverTargetRoute::Window) {
            currentTarget_ = windowTarget;
            currentBrowserWindow_ = nullptr;
            overlay_.updateHighlight(windowTarget->captureBounds, false);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool windowChanged = currentBrowserWindow_ != windowTarget->window ||
                                   lastBrowserWindowBounds_ != windowTarget->windowBounds;
        const auto detection = browserDetector_.latestResult();
        const bool hasResult = !windowChanged && detection.has_value() &&
                               isCurrentBrowserDetection(*detection, currentBrowserGeneration_, windowTarget->window);
        if (windowChanged) {
            currentBrowserBounds_.reset();
        } else if (hasResult) {
            currentBrowserBounds_.reset();
            if (detection->bounds.has_value()) {
                const PixelRect clipped = intersectRect(*detection->bounds, windowTarget->windowBounds);
                if (!clipped.isEmpty()) {
                    currentBrowserBounds_ = clipped;
                }
            }
        }

        const bool pointerMoved = cursor.x != lastBrowserPoint_.x || cursor.y != lastBrowserPoint_.y;
        // Let an in-flight query finish before replacing its generation. Retry
        // stationary points too, because accessibility can become ready later
        // and scrolling can change the element without moving the cursor.
        if (windowChanged || (hasResult &&
            (pointerMoved || now - browserRequestStarted_ >= std::chrono::milliseconds{150}))) {
            currentBrowserWindow_ = windowTarget->window;
            lastBrowserWindowBounds_ = windowTarget->windowBounds;
            lastBrowserPoint_ = cursor;
            currentBrowserGeneration_ = browserDetector_.submit(windowTarget->window, cursor);
            browserRequestStarted_ = now;
        }
        currentTarget_ = windowTarget;
        bool isPending = !hasResult && now - browserRequestStarted_ < std::chrono::seconds{2};
        if (currentBrowserBounds_.has_value() && cursor.x >= currentBrowserBounds_->left &&
            cursor.x < currentBrowserBounds_->right && cursor.y >= currentBrowserBounds_->top &&
            cursor.y < currentBrowserBounds_->bottom) {
            currentTarget_ = CaptureTarget{CaptureTargetKind::BrowserElement, windowTarget->window,
                                           windowTarget->windowBounds, *currentBrowserBounds_};
            isPending = false;
        }
        overlay_.updateHighlight(currentTarget_->captureBounds, isPending);
    }

    void captureCurrentTarget() {
        const CaptureTarget target = *currentTarget_;
        capturedWindowTitle_ = windowTitle(target.window);
        inputHook_.remove();
        overlay_.hide();
        std::string error;
        capturedImage_ = captureWindow(target, error);
        if (!capturedImage_.has_value()) {
            sessionState_.reset();
            reportError(error);
            return;
        }
        static_cast<void>(sessionState_.startReviewing());
        showReview(target.captureBounds);
        spdlog::info("Capture completed: {}x{}", capturedImage_->width, capturedImage_->height);
    }

    void showReview(PixelRect targetBounds) {
        constexpr int width = 244;
        constexpr int height = 64;
        if (reviewWindow_ == nullptr) {
            reviewWindow_ = std::make_unique<ReviewWindow>(*this, width, height);
            reviewWindow_->label("Deep Sniper - Capture ready");
            reviewWindow_->border(0);
            reviewWindow_->color(kPanel);
            reviewWindow_->begin();
            auto* saveDefault = new ThemedButton(8, 8, 48, 48, "Save default", ButtonIcon::Save);
            auto* saveAs = new ThemedButton(64, 8, 48, 48, "Save As", ButtonIcon::SaveAs);
            saveAsButton_ = saveAs;
            auto* copy = new ThemedButton(120, 8, 48, 48, "Copy", ButtonIcon::Copy);
            auto* divider = new Fl_Box(180, 20, 1, 24);
            divider->box(FL_FLAT_BOX);
            divider->color(UiTheme::kBorder);
            auto* cancel = new ThemedButton(188, 8, 48, 48, "Discard", ButtonIcon::Close);
            saveDefault->color(kAccent);
            saveDefault->labelcolor(kBackground);
            cancel->labelcolor(kDanger);
            saveDefault->tooltip("Save to default folder (Ctrl+S)");
            saveAs->tooltip("Save as... (Ctrl+Shift+S)");
            copy->tooltip("Copy image (Ctrl+C)");
            cancel->tooltip("Discard capture (Esc)");
            saveDefault->shortcut(FL_CTRL | 's');
            saveAs->shortcut(FL_CTRL | FL_SHIFT | 's');
            copy->shortcut(FL_CTRL | 'c');
            saveDefault->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->saveDefault(); }, this);
            saveAs->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->saveAs(); }, this);
            copy->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->copyToClipboard(); }, this);
            cancel->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->cancelReview(); }, this);
            reviewWindow_->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->cancelReview(); }, this);
            reviewWindow_->end();
        }
        if (destinationRow_ != nullptr) {
            reviewWindow_->remove(destinationRow_);
            delete destinationRow_;
            destinationRow_ = nullptr;
        }
        destinationsShown_ = false;
        reviewWindow_->size(width, height);
        const PixelRect workArea = monitorWorkAreaFor(targetBounds);
        const int x = std::clamp(targetBounds.right - width, workArea.left, workArea.right - width);
        int y = targetBounds.bottom + 8;
        if (y + height > workArea.bottom) {
            y = targetBounds.top - height - 8;
        }
        y = std::clamp(y, workArea.top, workArea.bottom - height);
        reviewWindow_->position(x, y);
        reviewWindow_->show();
        reviewWindow_->take_focus();
    }

    void showDestinations() {
        destinationsShown_ = true;
        const PixelRect workArea = monitorWorkAreaFor({reviewWindow_->x(), reviewWindow_->y(),
            reviewWindow_->x() + 244, reviewWindow_->y() + 64});
        const int configuredCount = static_cast<int>(settings_.destinationFolders.size());
        const int recentCount = static_cast<int>(settings_.recentFolders.size());
        const int configuredWidth = std::max(180, configuredCount * 112);
        const int recentWidth = std::max(180, recentCount * 112);
        const int width = std::min(configuredWidth + recentWidth + 40, workArea.width());
        const int x = std::clamp(reviewWindow_->x(), workArea.left, workArea.right - width);
        const int y = std::clamp(reviewWindow_->y(), workArea.top, workArea.bottom - 168);
        reviewWindow_->resize(x, y, width, 168);
        reviewWindow_->begin();
        destinationRow_ = new Fl_Scroll(0, 64, width, 104);
        destinationRow_->type(Fl_Scroll::HORIZONTAL);
        destinationRow_->color(kPanel);
        destinationRow_->begin();
        addLabel(12, 68, configuredWidth, 18, "YOUR FOLDERS", kAccent)->labelsize(10);
        const int recentX = configuredWidth + 28;
        addLabel(recentX, 68, recentWidth, 18, "RECENT SAVE AS", kMuted)->labelsize(10);
        auto* divider = new Fl_Box(configuredWidth + 18, 72, 1, 64);
        divider->box(FL_FLAT_BOX);
        divider->color(UiTheme::kBorder);
        destinationButtons_.fill(nullptr);
        int index = 0;
        const auto addFolders = [&](const auto& folders, int startX) {
            int column = 0;
            for (const auto& folder : folders) {
                reviewFolders_[index] = folder;
                const std::string name = wideToUtf8(folder.filename().empty() ? folder.wstring() : folder.filename().wstring());
                auto* button = new ThemedButton(startX + column * 112, 92, 104, 44, "", ButtonIcon::Destination);
                button->copy_label(name.c_str());
                button->labelsize(11);
                button->copy_tooltip(wideToUtf8(folder.wstring()).c_str());
                button->callback([](Fl_Widget* widget, void* value) {
                    auto& app = *static_cast<App*>(value);
                    const auto found = std::find(app.destinationButtons_.begin(), app.destinationButtons_.end(), widget);
                    if (found != app.destinationButtons_.end()) {
                        Settings destination = app.settings_;
                        destination.defaultSaveFolder = app.reviewFolders_[found - app.destinationButtons_.begin()];
                        const auto path = defaultCapturePath(destination, app.capturedWindowTitle_);
                        if (!path.empty()) {
                            app.saveCapturedImage(path, destination.defaultFormat);
                        } else {
                            app.reportError("Unable to find a free filename in this folder.");
                        }
                    }
                }, this);
                destinationButtons_[index++] = button;
                ++column;
            }
        };
        addFolders(settings_.destinationFolders, 12);
        addFolders(settings_.recentFolders, recentX);
        if (configuredCount == 0) {
            addLabel(12, 92, 176, 44, "Add folders in Settings", kMuted);
        }
        if (recentCount == 0) {
            addLabel(recentX, 92, 176, 44, "Save As folders appear here", kMuted);
        }
        destinationRow_->end();
        reviewWindow_->end();
        reviewWindow_->redraw();
    }

    void showSettings() {
        if (sessionState_.value() != CaptureState::Idle) {
            return;
        }
        if (settingsWindow_ == nullptr) {
            buildSettingsWindow();
        }
        const std::string folder = wideToUtf8(settings_.defaultSaveFolder.wstring());
        folderInput_->value(folder.c_str());
        formatChoice_->value(settings_.defaultFormat == ImageFormat::Png ? 0 : 1);
        titleLengthInput_->value(std::to_string(settings_.titleLength).c_str());
        for (std::size_t index = 0; index < destinationInputs_.size(); ++index) {
            destinationInputs_[index]->value(index < settings_.destinationFolders.size()
                ? wideToUtf8(settings_.destinationFolders[index].wstring()).c_str() : "");
        }
        hotkeyButton_->setHotkey(settings_.captureHotkey);
        updateHotkeyStatus();
        int mouseX{}, mouseY{}, screenX{}, screenY{}, screenWidth{}, screenHeight{};
        Fl::get_mouse(mouseX, mouseY);
        const int screen = Fl::screen_num(mouseX, mouseY);
        settingsWindow_->screen_num(screen);
        Fl::screen_work_area(screenX, screenY, screenWidth, screenHeight, screen);
        settingsWindow_->position(screenX + (screenWidth - settingsWindow_->w()) / 2,
                                  screenY + (screenHeight - settingsWindow_->h()) / 2);
        settingsWindow_->show();
        settingsWindow_->take_focus();
    }

    void buildSettingsWindow() {
        settingsWindow_ = std::make_unique<SettingsWindow>(1000, 590, "Deep Sniper Settings");
        settingsWindow_->color(kBackground);
        settingsWindow_->begin();
        auto* destinationsPanel = new Fl_Box(600, 120, 376, 380);
        destinationsPanel->box(FL_FLAT_BOX);
        destinationsPanel->color(kPanel);
        addLabel(620, 136, 336, 22, "Quick-save folders")->labelfont(FL_HELVETICA_BOLD);
        addLabel(620, 162, 336, 34, "Choose up to 7 destinations. Clear a field to remove it.", kMuted);
        for (std::size_t index = 0; index < destinationInputs_.size(); ++index) {
            const int y = 206 + static_cast<int>(index) * 40;
            destinationInputs_[index] = new Fl_Input(620, y, 286, 30);
            destinationInputs_[index]->box(FL_FLAT_BOX);
            destinationInputs_[index]->color(UiTheme::kControl);
            destinationInputs_[index]->textcolor(kText);
            destinationInputs_[index]->textsize(12);
            auto* browse = new ThemedButton(916, y, 40, 30, "Choose folder", ButtonIcon::Folder);
            browse->tooltip("Choose quick-save folder");
            destinationBrowseButtons_[index] = browse;
            browse->callback([](Fl_Widget* widget, void* value) {
                auto& app = *static_cast<App*>(value);
                const auto found = std::find(app.destinationBrowseButtons_.begin(), app.destinationBrowseButtons_.end(), widget);
                const auto folder = chooseFolder(fl_xid(app.settingsWindow_.get()));
                if (folder && found != app.destinationBrowseButtons_.end()) {
                    app.destinationInputs_[found - app.destinationBrowseButtons_.begin()]->value(wideToUtf8(folder->wstring()).c_str());
                }
            }, this);
        }
        addLabel(28, 18, 544, 18, "DEEP SNIPER", kMuted)->labelsize(11);
        auto* heading = addLabel(28, 42, 544, 32, "Settings");
        heading->labelsize(26);
        heading->labelfont(FL_HELVETICA_BOLD);
        addLabel(28, 82, 544, 20, "Your captures, saved your way.", kMuted)->labelsize(13);
        for (const PixelRect card : {PixelRect{24, 120, 576, 374}, PixelRect{24, 390, 576, 500}}) {
            auto* panel = new Fl_Box(card.left, card.top, card.width(), card.height());
            panel->box(FL_FLAT_BOX);
            panel->color(kPanel);
        }
        addLabel(44, 136, 500, 20, "Save location")->labelfont(FL_HELVETICA_BOLD);
        auto* folderSurface = new Fl_Box(44, 166, 436, 40);
        folderSurface->box(FL_FLAT_BOX);
        folderSurface->color(UiTheme::kControl);
        folderInput_ = new Fl_Input(54, 176, 416, 20);
        folderInput_->box(FL_FLAT_BOX);
        folderInput_->color(UiTheme::kControl);
        folderInput_->textcolor(kText);
        folderInput_->textsize(13);
        folderInput_->cursor_color(kAccent);
        folderInput_->selection_color(fl_rgb_color(46, 103, 92));
        folderInput_->tooltip("Default screenshot folder");
        auto* browseButton = new ThemedButton(492, 166, 60, 40, "Browse folder", ButtonIcon::Folder);
        browseButton->tooltip("Choose default save folder");
        browseButton->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->browseFolder(); }, this);
        addLabel(44, 230, 300, 20, "Image format")->labelfont(FL_HELVETICA_BOLD);
        addLabel(44, 252, 300, 18, "PNG for detail. JPEG for smaller files.", kMuted);
        formatChoice_ = new ThemedChoice(380, 232, 172, 40);
        formatChoice_->box(FL_FLAT_BOX);
        formatChoice_->down_box(FL_FLAT_BOX);
        formatChoice_->color(UiTheme::kControl);
        formatChoice_->textcolor(kText);
        formatChoice_->textsize(13);
        formatChoice_->selection_color(UiTheme::kHover);
        formatChoice_->tooltip("Default image format");
        formatChoice_->add("PNG");
        formatChoice_->add("JPEG");
        addLabel(44, 300, 300, 20, "Title characters in filename")->labelfont(FL_HELVETICA_BOLD);
        addLabel(44, 324, 380, 18, "The page or window title is followed by the timestamp.", kMuted);
        titleLengthInput_ = new Fl_Input(452, 300, 100, 40);
        titleLengthInput_->box(FL_FLAT_BOX);
        titleLengthInput_->color(UiTheme::kControl);
        titleLengthInput_->textcolor(kText);
        titleLengthInput_->textsize(13);
        titleLengthInput_->tooltip("Use between 1 and 200 title characters");
        addLabel(44, 408, 236, 20, "Capture shortcut")->labelfont(FL_HELVETICA_BOLD);
        addLabel(44, 432, 236, 18, "Click the key to record a shortcut.", kMuted);
        hotkeyButton_ = new HotkeyButton(300, 408, 252, 42);
        hotkeyButton_->tooltip("Click, then press your preferred capture shortcut");
        hotkeyStatus_ = addLabel(44, 464, 508, 20, "", kMuted);
        auto* cancelButton = new ThemedButton(316, 528, 108, 38, "Cancel");
        auto* saveButton = new ThemedButton(436, 528, 140, 38, "Save changes");
        saveButton->color(kAccent);
        saveButton->labelcolor(kBackground);
        saveButton->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->saveSettings(); }, this);
        cancelButton->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->settingsWindow_->hide(); }, this);
        settingsWindow_->callback([](Fl_Widget*, void* value) { static_cast<App*>(value)->settingsWindow_->hide(); }, this);
        settingsWindow_->end();

    }

    void browseFolder() {
        const auto folder = chooseFolder(settingsWindow_ == nullptr ? nullptr : fl_xid(settingsWindow_.get()));
        if (folder.has_value()) {
            const std::string value = wideToUtf8(folder->wstring());
            folderInput_->value(value.c_str());
        }
    }

    void saveSettings() {
        try {
            const std::uint32_t titleLength = static_cast<std::uint32_t>(std::stoul(titleLengthInput_->value()));
            Settings candidate{std::filesystem::path{utf8ToWide(folderInput_->value())}, formatChoice_->value() == 0 ? ImageFormat::Png : ImageFormat::Jpeg, hotkeyButton_->hotkey(), titleLength};
            candidate.recentFolders = settings_.recentFolders;
            for (const auto* input : destinationInputs_) {
                if (input->value()[0] != '\0') {
                    const std::filesystem::path folder{utf8ToWide(input->value())};
                    if (!folder.is_absolute()) {
                        throw std::runtime_error("Quick-save destinations must be absolute folder paths.");
                    }
                    if (std::find(candidate.destinationFolders.begin(), candidate.destinationFolders.end(), folder) == candidate.destinationFolders.end()) {
                        candidate.destinationFolders.push_back(folder);
                    }
                }
            }
            settingsStore_.save(candidate);
            settings_ = std::move(candidate);
            isHotkeyRegistered_ = tray_.registerCaptureHotkey(settings_.captureHotkey);
            updateHotkeyStatus();
            if (isHotkeyRegistered_) {
                settingsWindow_->hide();
            } else {
                tray_.showNotification(
                    L"Deep Sniper hotkey unavailable",
                    L"The selected shortcut is already registered by another application.");
            }
            spdlog::info("Settings saved");
        } catch (const std::exception& error) {
            reportError(error.what());
        }
    }

    void updateHotkeyStatus() {
        hotkeyStatus_->labelcolor(isHotkeyRegistered_ ? kMuted : kDanger);
        hotkeyStatus_->copy_label(isHotkeyRegistered_ ? "The shortcut is active system-wide." : "This shortcut is unavailable; choose another one.");
    }

    void saveDefault() {
        const std::filesystem::path path = defaultCapturePath(settings_, capturedWindowTitle_);
        if (path.empty()) {
            reportError("Unable to find a free filename in the default folder.");
            return;
        }
        saveCapturedImage(path, settings_.defaultFormat);
    }
    void saveAs() {
        const auto selection = chooseSavePath(reviewWindow_ == nullptr ? nullptr : fl_xid(reviewWindow_.get()), settings_, capturedWindowTitle_);
        if (selection.has_value()) {
            saveCapturedImage(selection->first, selection->second, true);
        }
    }
    void saveCapturedImage(const std::filesystem::path& path, ImageFormat format, bool rememberFolder = false) {
        if (!capturedImage_.has_value()) {
            return;
        }
        std::string error;
        if (!saveImage(*capturedImage_, path, format, error)) {
            reportError(error);
            return;
        }
        tray_.showNotification(L"Capture saved", path.c_str(), NIIF_INFO);
        if (rememberFolder) {
            const auto folder = path.parent_path();
            std::erase(settings_.recentFolders, folder);
            settings_.recentFolders.insert(settings_.recentFolders.begin(), folder);
            if (settings_.recentFolders.size() > 3) {
                settings_.recentFolders.resize(3);
            }
            try {
                settingsStore_.save(settings_);
            } catch (const std::exception& error) {
                spdlog::warn("Capture saved but recent folders could not be persisted: {}", error.what());
            }
        }
        spdlog::info("Capture saved to {}", wideToUtf8(path.wstring()));
        cancelCapture();
    }
    void copyToClipboard() {
        if (!capturedImage_.has_value()) {
            return;
        }
        std::string error;
        if (!copyImageToClipboard(*capturedImage_, error)) {
            reportError(error);
            return;
        }
        tray_.showNotification(L"Capture copied", L"The image is now on the clipboard.", NIIF_INFO);
        spdlog::info("Capture copied to clipboard");
        cancelCapture();
    }
    void requestExit() {
        cancelCapture();
        if (settingsWindow_ != nullptr) {
            settingsWindow_->hide();
        }
        isRunning_ = false;
    }
    void reportError(std::string_view message) {
        spdlog::error("{}", message);
        fl_alert("%s", std::string{message}.c_str());
    }
};

int ReviewWindow::handle(int event) {
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
        app_.cancelReview();
        return 1;
    }
    return Fl_Double_Window::handle(event);
}

void dispatchNativeMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        InstanceMutex instanceMutex;
        if (instanceMutex.alreadyExists()) {
            if (const HWND existingWindow = FindWindowW(kTrayWindowClass, kApplicationName); existingWindow != nullptr) {
                PostMessageW(existingWindow, kActivateExistingInstanceMessage, 0, 0);
            }
            if (SUCCEEDED(comResult)) {
                CoUninitialize();
            }
            return 0;
        }
        App app;
        while (app.isRunning()) {
            const DWORD waitMilliseconds = app.isSelecting() ? 16U : (app.hasVisibleWindow() ? 50U : 250U);
            MsgWaitForMultipleObjectsEx(0, nullptr, waitMilliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            dispatchNativeMessages();
            Fl::check();
            app.tick();
        }
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 0;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Deep Sniper", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 1;
    }
}
