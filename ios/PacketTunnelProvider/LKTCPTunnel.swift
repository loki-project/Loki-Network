import NetworkExtension

final class LKTCPTunnel : NSObject {
    private let provider: NEPacketTunnelProvider
    private let configuration: Configuration
    private var connection: NWTCPConnection?
    private var openCompletion: ((Error?) -> Void)?
    
    // MARK: Initialization
    init(provider: NEPacketTunnelProvider, configuration: Configuration = Configuration()) {
        self.provider = provider
        self.configuration = configuration
    }
    
    // MARK: Connection
    func open(_ completion: @escaping (Error?) -> Void) {
        openCompletion = completion
        let endpoint = NWHostEndpoint(hostname: configuration.address, port: String(configuration.port))
        connection = provider.createTCPConnection(to: endpoint, enableTLS: false, tlsParameters: nil, delegate: nil)
        connection!.addObserver(self, forKeyPath: "state", options: .initial, context: &connection)
    }
    
    override func observeValue(forKeyPath keyPath: String?, of object: Any?, change: [NSKeyValueChangeKey:Any]?, context: UnsafeMutableRawPointer?) {
        if keyPath == "state" && context?.assumingMemoryBound(to: Optional<NWTCPConnection>.self).pointee == connection {
            let connection = self.connection!
            let state = connection.state
            Console.log("[Loki] TCP Tunnel state changed to \(state).")
            switch state {
            case .connected:
                openCompletion?(nil)
                openCompletion = nil
                read()
            case .disconnected:
                if let error = connection.error {
                    Console.log("[Loki] Disconnected due to error: \(error).")
                }
                close()
            case .cancelled: close()
            default: break
            }
        } else {
            super.observeValue(forKeyPath: keyPath, of: object, change: change, context: context)
        }
    }
    
    private func read() {
        connection!.readMinimumLength(0, maximumLength: configuration.readBufferSize) { [weak self] data, error in
            if let data = data {
                Console.log(String(data: data, encoding: .utf8)!)
                self?.read()
            } else if let error = error {
                Console.log("[Loki] Couldn't read packet due to error: \(error).")
            } else {
                Console.log("[Loki] Couldn't read packet.")
            }
        }
    }
    
    func close() {
        connection?.cancel()
        openCompletion?("Failed to open tunnel.")
        openCompletion = nil
        connection?.removeObserver(self, forKeyPath: "state")
        connection = nil
    }
}
