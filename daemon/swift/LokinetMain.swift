// AppDelegateExtension.swift
// lifed from yggdrasil network ios port
//

import Foundation
import NetworkExtension
import Lokinet

class LokinetMain: PlatformAppDelegate {
    var vpnManager: NETunnelProviderManager = NETunnelProviderManager()

    let lokinetComponent = "org.lokinet.NetworkExtension"
    var lokinetAdminTimer: DispatchSourceTimer?


    func runMain() {
        print("Starting up lokinet")
        NETunnelProviderManager.loadAllFromPreferences { (savedManagers: [NETunnelProviderManager]?, error: Error?) in
            if let error = error {
                print(error)
            }

            if let savedManagers = savedManagers {
                for manager in savedManagers {
                    if (manager.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier == self.lokinetComponent {
                        print("Found saved VPN Manager")
                        self.vpnManager = manager
                    }
                }
            }

            self.vpnManager.loadFromPreferences(completionHandler: { (error: Error?) in
                if let error = error {
                    print(error)
                }

                self.vpnManager.localizedDescription = "Lokinet"
                self.vpnManager.isEnabled = true
            })
        }
    }
}
