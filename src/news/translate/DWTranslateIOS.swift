// On-device EN→FR translation inside the iOS app (native parity, 2026-11-22). macOS runs dw-translate.swift as a
// helper process; an iOS app cannot start one, so the same protocol runs here, in process, built into a static library
// linked into Deepwater (CMakeLists.txt) and driven by news/Translate.cpp:
//
//   dw_translate_start(lineCallback)        once; the first line sent back is {"status":"installed"|"supported"|"unsupported"}
//   dw_translate_request("{\"id\":7,\"text\":\"…\"}")
//                                           answered by {"id":7,"text":"…"} or {"id":7,"error":"…"}
//
// Lines are handed to the callback as UTF-8 C strings, from any thread (Translate.cpp copies them to the GUI thread).
// TranslationSession(installedSource:target:) exists from iOS 26: earlier systems answer "unsupported".
// Everything runs on the device; no text leaves the iPhone.
import Foundation
import Translation

public typealias DWLineCallback = @convention(c) (UnsafePointer<CChar>) -> Void

nonisolated(unsafe) private var dwCallback: DWLineCallback?
nonisolated(unsafe) private var dwStart: Task<Void, Never>?

private func dwEmit(_ obj: [String: Any]) {
  guard let cb = dwCallback, let d = try? JSONSerialization.data(withJSONObject: obj),
        let s = String(data: d, encoding: .utf8) else { return }
  s.withCString { cb($0) }
}

@available(iOS 26.0, *)
private actor DWTranslator {
  static let shared = DWTranslator()
  private var session: TranslationSession?

  func start(_ source: String, _ target: String) async {
    let src = Locale.Language(identifier: source)
    let dst = Locale.Language(identifier: target)
    let status = await LanguageAvailability().status(from: src, to: dst)
    switch status {
    case .installed: dwEmit(["status": "installed"])
    case .supported: dwEmit(["status": "supported"])
    default: dwEmit(["status": "unsupported"])
    }
    if status == .installed { session = TranslationSession(installedSource: src, target: dst) }
  }

  func translate(_ id: Int, _ text: String) async {
    guard let session else { dwEmit(["id": id, "error": "language pack not installed"]); return }
    do {
      let r = try await session.translate(text)
      dwEmit(["id": id, "text": r.targetText])
    } catch {
      dwEmit(["id": id, "error": "\(error)"])
    }
  }
}

@_cdecl("dw_translate_start")
public func dwTranslateStart(_ callback: DWLineCallback) {
  dwCallback = callback
  dwStart = Task {
    if #available(iOS 26.0, *) {
      await DWTranslator.shared.start("en", "fr")
    } else {
      dwEmit(["status": "unsupported"])
    }
  }
}

@_cdecl("dw_translate_request")
public func dwTranslateRequest(_ line: UnsafePointer<CChar>) {
  let s = String(cString: line)
  let started = dwStart
  Task {
    await started?.value   // requests sent before the status line wait for the session
    guard let data = s.data(using: .utf8),
          let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let id = obj["id"] as? Int, let text = obj["text"] as? String else { return }
    if #available(iOS 26.0, *) {
      await DWTranslator.shared.translate(id, text)
    } else {
      dwEmit(["id": id, "error": "iOS 26 or later is needed"])
    }
  }
}
