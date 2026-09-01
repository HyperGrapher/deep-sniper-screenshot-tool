#include "browser_detector.hpp"

#include <uiautomation.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>

#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] std::wstring processExecutableName(HWND window) {
    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return {};
    }
    std::wstring path(32768, L'\0');
    DWORD pathSize = static_cast<DWORD>(path.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &pathSize);
    CloseHandle(process);
    if (!queried) {
        return {};
    }
    path.resize(pathSize);
    std::wstring name = std::filesystem::path{path}.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    return name;
}

}  // namespace

bool isBrowserWindow(HWND window) {
    return isBrowserExecutableName(processExecutableName(window));
}

bool isBrowserExecutableName(std::wstring_view executableName) {
    constexpr std::array browsers{L"chrome.exe", L"vivaldi.exe", L"brave.exe", L"firefox.exe"};
    std::wstring normalized{executableName};
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::towlower);
    return std::ranges::find(browsers, normalized) != browsers.end();
}

bool isPointInWindowClientArea(HWND window, POINT point) {
    RECT client{};
    if (!GetClientRect(window, &client)) {
        return false;
    }
    POINT topLeft{client.left, client.top};
    POINT bottomRight{client.right, client.bottom};
    if (!ClientToScreen(window, &topLeft) || !ClientToScreen(window, &bottomRight)) {
        return false;
    }
    const RECT screenClient{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    return PtInRect(&screenClient, point) != FALSE;
}

bool isCurrentBrowserDetection(const BrowserDetectionResult& result, std::uint64_t generation, HWND window) {
    return result.generation == generation && result.window == window;
}

HoverTargetRoute routeHoverTarget(bool isBrowser, bool isClientArea) {
    return isBrowser && isClientArea ? HoverTargetRoute::BrowserElement : HoverTargetRoute::Window;
}

BrowserElementDetector::BrowserElementDetector() : worker_([this](std::stop_token token) { run(token); }) {}

std::uint64_t BrowserElementDetector::submit(HWND window, POINT point) {
    std::lock_guard lock{mutex_};
    query_ = Query{++nextGeneration_, window, point};
    condition_.notify_one();
    return query_.generation;
}

std::optional<BrowserDetectionResult> BrowserElementDetector::latestResult() const {
    std::lock_guard lock{mutex_};
    return result_;
}

void BrowserElementDetector::run(std::stop_token stopToken) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IUIAutomation> automation;
    if (SUCCEEDED(comResult)) {
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    }

    std::uint64_t processedGeneration{};
    while (!stopToken.stop_requested()) {
        Query query;
        {
            std::unique_lock lock{mutex_};
            condition_.wait(lock, stopToken, [&] { return query_.generation > processedGeneration; });
            if (stopToken.stop_requested()) {
                break;
            }
            query = query_;
        }

        BrowserDetectionResult detection{query.generation, query.window, std::nullopt};
        if (automation != nullptr && IsWindow(query.window)) {
            ComPtr<IUIAutomationElement> element;
            if (SUCCEEDED(automation->ElementFromPoint(query.point, &element)) && element != nullptr) {
                bool belongsToDocument = false;
                ComPtr<IUIAutomationTreeWalker> walker;
                automation->get_ControlViewWalker(&walker);
                ComPtr<IUIAutomationElement> ancestor = element;
                for (int depth = 0; depth < 32 && ancestor != nullptr; ++depth) {
                    CONTROLTYPEID controlType{};
                    if (SUCCEEDED(ancestor->get_CurrentControlType(&controlType)) && controlType == UIA_DocumentControlTypeId) {
                        belongsToDocument = true;
                        break;
                    }
                    ComPtr<IUIAutomationElement> parent;
                    if (walker == nullptr || FAILED(walker->GetParentElement(ancestor.Get(), &parent))) {
                        break;
                    }
                    ancestor = std::move(parent);
                }
                RECT bounds{};
                if (belongsToDocument && SUCCEEDED(element->get_CurrentBoundingRectangle(&bounds))) {
                    const PixelRect pixelBounds{bounds.left, bounds.top, bounds.right, bounds.bottom};
                    if (!pixelBounds.isEmpty()) {
                        detection.bounds = pixelBounds;
                    }
                }
            }
        }
        {
            std::lock_guard lock{mutex_};
            result_ = detection;
        }
        processedGeneration = query.generation;
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
}
