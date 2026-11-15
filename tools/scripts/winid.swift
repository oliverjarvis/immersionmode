// Prints "id<TAB>title<TAB>width<TAB>height" for every Azahar window, for screencapture -l.
import CoreGraphics

let windows = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] ?? []
for window in windows {
    let owner = (window[kCGWindowOwnerName as String] as? String ?? "").lowercased()
    guard owner.contains("azahar") else { continue }
    let bounds = window[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let title = window[kCGWindowName as String] as? String ?? ""
    let number = window[kCGWindowNumber as String] as? Int ?? 0
    let width = bounds["Width"] as? Int ?? 0
    let height = bounds["Height"] as? Int ?? 0
    print("\(number)\t\(title)\t\(width)\t\(height)")
}
