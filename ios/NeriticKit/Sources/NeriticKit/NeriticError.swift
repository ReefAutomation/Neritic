import Foundation

public enum NeriticError: Error, Equatable, Sendable {
    /// The device answered, but not with success.
    case http(status: Int, message: String?)
    /// The request never reached the device (offline, wrong host, refused).
    case unreachable(underlying: String)
    /// The connection was established and then dropped mid-request.
    ///
    /// Distinct from ``unreachable`` because it is the expected signature of a
    /// restart: the device replies to a reboot command and then restarts, so the
    /// socket frequently dies before the response is fully read.
    case connectionLost
    /// The device answered with something the client could not decode.
    case decoding(underlying: String)
    /// The request was cancelled (for example the view went away).
    case cancelled
    /// The device reported an application-level failure.
    case device(message: String)

    public var userFacingDescription: String {
        switch self {
        case let .http(status, message):
            if let message, !message.isEmpty {
                return "The device returned \(status): \(message)"
            }
            return "The device returned HTTP \(status)."
        case .unreachable:
            return "Could not reach the device. Check that it is powered on and on the same network."
        case .connectionLost:
            return "The connection to the device dropped."
        case let .decoding(underlying):
            return "The device sent an unexpected reply (\(underlying))."
        case .cancelled:
            return "Request cancelled."
        case let .device(message):
            return message
        }
    }

    /// True when retrying might succeed, used to decide whether to surface a
    /// reconnect prompt or simply keep the last known state.
    public var isRetryable: Bool {
        switch self {
        case .unreachable: return true
        case .connectionLost: return true
        case let .http(status, _): return status >= 500
        case .decoding, .cancelled, .device: return false
        }
    }
}
