# Sante_iOT

**État de santé d'un iOT** — librairie **header-only** pour **ESP8266** et **ESP32** (classe `Sante_iOT` + instance globale `sante_iOT`).

Agrège l'état de santé d'un nœud en un seul point : **compteurs ESP-NOW**, **variables applicatives** (via pointeurs), **infos/logs WiFiManagerESP** et **valeurs génériques** enregistrables — avec un **score de santé 0-100** et des **seuils configurables**.

Auteur : Olivier FOURNET — Licence GPL-3.0-only.

## Fonctionnalités

- **Compteurs ESP-NOW** attachés par pointeurs de fonction (découplé d'`espNow_SharedVar`) :
  `rxTotal`, `rxCtrl`, `rxData`, `rxJete`, `rxOverflow`, `nbPeers`, `enLigne`, dernière réception.
- **Variables applicatives via pointeurs** : `t_fct` (uptime), `rssi_moy`, `heap` — sans copie.
- **Infos/logs WiFiManagerESP** (v0.7.9+) : connecté/SSID/RSSI, **compteur de resets WiFi**
  (chaque reset efface ESP-NOW), horodatage du dernier reset, **logs/historique** formatable.
- **Valeurs génériques enregistrables** (`ajouterValeur(nom, getter, unité)`) : extensible sans
  toucher la lib — état MCP/Telegram, uptime précis, etc.
- **Métriques dérivées** : `tauxPerteRx()` (0..1), `tauxValiditeDonnees()` (0..1),
  `ageDerniereRecv()` (s).
- **Score de santé 0-100** + `enSante()` booléen, **seuils configurables** (heap min, RSSI min,
  taux de perte max).
- **Sorties** : `rapport()` (texte multi-lignes — Telegram, MCP), `resume()` (1 ligne — heartbeat),
  `json(obj)` (template ArduinoJson v7 — web).

## Installation

### PlatformIO

Copier la lib dans `lib/` du projet, ou via `lib_deps` :

```ini
lib_deps =
    https://github.com/Fo170/Sante_iOT.git@^1.0.0
```

### Arduino IDE

Copier le dossier `Sante_iOT` dans `libraries/`.

## Utilisation rapide

```cpp
#include "Sante_iOT.h"
Sante_iOT sante;

void setup() {
  sante.setNom("ESP32-Ordonnanceur");
  sante.setVersion(version_str.c_str());
  sante.setUptimePtr(&t_fct);
  sante.setRssiMoyPtr(&rssi_moy);
  sante.setHeapPtr(&heap_libre);

  // Compteurs ESP-NOW
  Sante_iOT::EspNowSante es;
  es.rxTotal = []() -> uint32_t { return esv.rxTotal(); };
  es.rxCtrl  = []() -> uint32_t { return esv.rxCtrl(); };
  es.rxData  = []() -> uint32_t { return esv.rxData(); };
  es.rxJete  = []() -> uint32_t { return esv.rxDataDrop(); };
  es.rxOverflow = []() -> uint32_t { return esv.rxOverflow(); };
  es.nbPeers = []() -> uint8_t { return esv.peerCount(); };
  es.enLigne = []() -> uint8_t { return esv.getOnlinePeerCount(); };
  es.derniereRecvMs = []() -> uint32_t { return espnow_last_recv; };
  sante.attacherEspNow(es);

  // Infos/logs WiFiManagerESP (v0.7.9)
  Sante_iOT::WiFiSante ws;
  ws.connecte  = []() -> bool     { return wifiManager.isConnected(); };
  ws.ssid      = []() -> String   { return wifiManager.getSSID(); };
  ws.rssi      = []() -> int8_t   { return wifiManager.getRSSI(); };
  ws.resets    = []() -> uint32_t { return wifiManager.getResetCount(); };
  ws.dernierResetMs = []() -> unsigned long { return wifiManager.getLastResetTime(); };
  ws.logs      = []() -> String { /* formater getHistoryEntry() ici */ };
  sante.attacherWiFi(ws);

  sante.ajouterValeur("resetsWiFi", []() -> float { return wifiManager.getResetCount(); }, "x");
}

void loop() {
  // Heartbeat debug :
  Serial.println(sante.resume());

  // API web (ArduinoJson v7) :
  // JsonDocument doc; sante.json(doc.as<JsonObject>()); serializeJson(doc, json);
}
```

## API

| Méthode | Description |
|---------|-------------|
| `setNom()` / `setVersion()` | Identité du nœud. |
| `setUptimePtr()` / `setRssiMoyPtr()` / `setHeapPtr()` | Pointeurs sur variables applicatives. |
| `attacherEspNow(EspNowSante)` | Compteurs ESP-NOW (pointeurs de fonction). |
| `attacherWiFi(WiFiSante)` | Infos/logs WiFiManagerESP (v0.7.9+). |
| `ajouterValeur(nom, getter, unité)` | Valeur générique enregistrable (max 8). |
| `tauxPerteRx()` | Part de paquets non traités (0..1). |
| `tauxValiditeDonnees()` | Validité des données reçues (0..1). |
| `ageDerniereRecv()` | Âge (s) de la dernière donnée reçue. |
| `scoreGlobal()` | Score de santé 0..100. |
| `enSante()` | `true` si score ≥ 60. |
| `rapport()` | Rapport texte multi-lignes. |
| `resume()` | Résumé sur une ligne. |
| `json(obj)` | Remplit un objet ArduinoJson v7. |

## Seuils (défauts)

| Seuil | Défaut | Via |
|-------|--------|-----|
| Heap libre min | 20 000 B | `setSeuilHeapMin()` |
| RSSI min | -85 dBm | `setSeuilRssiMin()` |
| Taux de perte RX max | 50 % | `setSeuilPerteMax()` |

## Versions

| Version | Date | Changements |
|---------|------|-------------|
| v1.0.0 | 2026-08 | Création : compteurs ESP-NOW (espNow_SharedVar v1.0.4), pointeurs applicatifs (t_fct, rssi_moy, heap), infos/logs WiFiManagerESP v0.7.9, valeurs génériques, score 0-100, sorties texte/résumé/JSON. |
