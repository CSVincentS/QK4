# network/

TCP / TLS / PSK transport to the K4, binary protocol framing, CAT server for third-party apps, DX cluster client, KPA1500 amplifier client, K4 mDNS discovery.

## Files

- `tcpclient.{cpp,h}` — TLS/PSK socket to K4. State machine with ARP cold-start retry. `sendCAT` + `sendRaw` public entry points.
- `protocol.{cpp,h}` — K4 binary packet framing (`START_MARKER` + big-endian length + `END_MARKER`), payload type dispatch, audio + miniPAN + spectrum routing.
- `catserver.{cpp,h}` — TCP CAT server for WSJT-X / MacLoggerDX integration. Pinned public API via `docs/radiostate-catserver-api-contract.md`, regression-gated by `test_catserver`.
- `networkmetrics.{cpp,h}` — Latency / throughput aggregation. Feeds `NetHealthWidget`.
- `k4discovery.{cpp,h}` — UDP mDNS discovery for K4 servers on the LAN.
- `dxclusterclient.{cpp,h}` — TCP client to a single DX cluster node. Used by `DxClusterController` — one instance per configured cluster.
- `kpa1500client.{cpp,h}` — TCP client to the KPA1500 amplifier.

## Threading

- `TcpClient` lives on `ConnectionController::m_ioThread`.
- `DxClusterClient` lives on a per-instance `QThread` spawned by `DxClusterController::ensureInstance`.
- `KPA1500Client` lives on its owning `KPA1500UiController`'s thread (main).

## K4 framing

4-byte `START_MARKER` (0xFE 0xFD 0xFC 0xFB) + 4-byte big-endian length + payload + 4-byte `END_MARKER`. Parser keeps the last 3 bytes of any unparsed buffer tail so partial markers don't lose sync across `readyRead` calls.

## CatServer contract

`docs/radiostate-catserver-api-contract.md` lists the 22 `RadioState` getters that `catserver.cpp` depends on. No signature or semantic change without updating the contract. `test_catserver` (33 cases) catches violations.

## See also

- `docs/radiostate-catserver-api-contract.md` — pinned `RadioState` API.
- `memory/k4-protocol-quirks.md` / `docs/k4-protocol-quirks.md` — K4 CAT oddities.
- `memory/threading-audit.md` — full thread map.
