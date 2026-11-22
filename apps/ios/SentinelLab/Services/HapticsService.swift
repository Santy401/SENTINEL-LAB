import CoreHaptics
import Foundation
import SentinelKit
import UIKit

/// Continuous haptic whose intensity follows the filtered phone shake, on the
/// same 0…1 scale as the firmware LED (|linear acceleration| / 8 m/s²).
/// Updates are rate-limited (≤ 20 Hz) and the haptic stops in Low Power Mode,
/// with Reduce Motion, when the user disables it, or when the app is inactive.
@MainActor
final class HapticsService {
    static let updateInterval: TimeInterval = 0.05

    private var engine: CHHapticEngine?
    private var player: CHHapticAdvancedPatternPlayer?
    private var lastUpdate = Date.distantPast
    private var lastIntensity: Float = 0
    private(set) var isPlaying = false

    var isSupported: Bool { CHHapticEngine.capabilitiesForHardware().supportsHaptics }

    /// True when system settings allow the effect at all.
    var systemAllows: Bool {
        !UIAccessibility.isReduceMotionEnabled && !ProcessInfo.processInfo.isLowPowerModeEnabled
    }

    var unavailableReason: String? {
        if !isSupported { return "Este iPhone no tiene motor háptico compatible." }
        if UIAccessibility.isReduceMotionEnabled { return "Desactivado por «Reducir movimiento»." }
        if ProcessInfo.processInfo.isLowPowerModeEnabled { return "Desactivado en modo de bajo consumo." }
        return nil
    }

    /// Feed the current filtered shake (m/s²). `enabled` combines the user
    /// toggle and the visibility of the Movimiento tab.
    func update(shakeMs2: Double, enabled: Bool) {
        guard enabled, isSupported, systemAllows else {
            stop()
            return
        }
        let now = Date()
        guard now.timeIntervalSince(lastUpdate) >= Self.updateInterval else { return }
        lastUpdate = now
        let intensity = Float(HapticMapping.intensity(shakeMs2: shakeMs2))
        if intensity <= 0 {
            if isPlaying { stop() }
            return
        }
        if !isPlaying { start() }
        guard isPlaying, abs(intensity - lastIntensity) >= 0.03 else { return }
        lastIntensity = intensity
        let params = [
            CHHapticDynamicParameter(parameterID: .hapticIntensityControl, value: intensity, relativeTime: 0),
            CHHapticDynamicParameter(parameterID: .hapticSharpnessControl, value: 0.25 + 0.6 * intensity, relativeTime: 0),
        ]
        try? player?.sendParameters(params, atTime: CHHapticTimeImmediate)
    }

    func stop() {
        guard isPlaying else { return }
        try? player?.stop(atTime: CHHapticTimeImmediate)
        isPlaying = false
        lastIntensity = 0
    }

    private func start() {
        do {
            if engine == nil {
                let engine = try CHHapticEngine()
                engine.playsHapticsOnly = true
                engine.isAutoShutdownEnabled = true
                engine.resetHandler = { [weak self] in
                    Task { @MainActor in
                        self?.player = nil
                        self?.isPlaying = false
                    }
                }
                engine.stoppedHandler = { [weak self] _ in
                    Task { @MainActor in self?.isPlaying = false }
                }
                self.engine = engine
            }
            try engine?.start()
            // A long continuous event; intensity is driven by dynamic params.
            let event = CHHapticEvent(eventType: .hapticContinuous, parameters: [
                CHHapticEventParameter(parameterID: .hapticIntensity, value: 1),
                CHHapticEventParameter(parameterID: .hapticSharpness, value: 0.4),
            ], relativeTime: 0, duration: 60)
            let pattern = try CHHapticPattern(events: [event], parameters: [])
            let player = try engine?.makeAdvancedPlayer(with: pattern)
            player?.loopEnabled = true
            try player?.start(atTime: CHHapticTimeImmediate)
            self.player = player
            isPlaying = true
        } catch {
            isPlaying = false
        }
    }
}
