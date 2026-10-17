// ============================================================================
//  Sante_iOT.h
//  État de santé d'un iOT — header-only, ESP8266 & ESP32 — v1.0.0
//
//  Agrège l'état de santé d'un nœud :
//    - identité (nom, version firmware)
//    - variables applicatives via POINTEURS (ex. t_fct, rssi_moy, heap)
//    - compteurs ESP-NOW attachés par pointeurs de fonction (découplé de
//      espNow_SharedVar : rxTotal, rxCtrl, rxData, rxJete, rxOverflow,
//      nbPeers, enLigne, dernière réception)
//    - valeurs génériques enregistrables (nom + getter + unité) : extensible
//      sans modifier la lib (ex. WiFiManagerESP.getResetCount(), état MCP...)
//
//  Métriques dérivées :
//    - tauxPerteRx()          : part de paquets non traités (perdus/débordés)
//    - tauxValiditeDonnees()  : data / (data + jeté)
//    - ageDerniereReception() : âge (s) de la dernière donnée reçue
//    - scoreGlobal() / enSante() : score 0-100 + seuils configurables
//
//  Sorties :
//    - rapport()  : texte multi-lignes (Telegram, /etat, MCP)
//    - resume()   : une seule ligne (heartbeat debug)
//    - json(obj)  : template ArduinoJson v7 (JsonDocument / JsonObject)
//
//  UTILISATION (inclusion UNIQUE par projet) :
//
//    #include "Sante_iOT.h"
//    Sante_iOT sante;
//
//    void setup() {
//      sante.setNom("ESP32-Ordonnanceur");
//      sante.setVersion(version_str.c_str());
//      sante.setUptimePtr(&t_fct);
//      sante.setRssiMoyPtr(&rssi_moy);          // dBm (int8_t)
//      sante.setHeapPtr(&heap_libre);           // octets (uint32_t)
//
//      Sante_iOT::EspNowSante es;
//      es.rxTotal = []() -> uint32_t { return esv.rxTotal(); };
//      es.rxCtrl  = []() -> uint32_t { return esv.rxCtrl();  };
//      es.rxData  = []() -> uint32_t { return esv.rxData();  };
//      es.rxJete  = []() -> uint32_t { return esv.rxDataDrop(); };
//      es.rxOverflow = []() -> uint32_t { return esv.rxOverflow(); };
//      es.nbPeers = []() -> uint8_t { return esv.peerCount(); };
//      es.enLigne = []() -> uint8_t { return esv.getOnlinePeerCount(); };
//      es.derniereRecvMs = []() -> uint32_t { return espnow_last_recv; };
//      // ⚠️ Sur ESP32, millis() retourne `unsigned long` ≠ `uint32_t` → toujours
//      // spécifier le type de retour `-> uint32_t` pour les getters numériques.
//      sante.attacherEspNow(es);
//
//      sante.ajouterValeur("resetsWiFi", []() -> float { return wifiManager.getResetCount(); }, "x");
//
//      // Infos/logs WiFiManagerESP (v0.7.9) :
//      Sante_iOT::WiFiSante ws;
//      ws.connecte  = []() -> bool     { return wifiManager.isConnected(); };
//      ws.ssid      = []() -> String   { return wifiManager.getSSID(); };
//      ws.rssi      = []() -> int8_t   { return wifiManager.getRSSI(); };
//      ws.resets    = []() -> uint32_t { return wifiManager.getResetCount(); };
//      ws.dernierResetMs = []() -> unsigned long { return wifiManager.getLastResetTime(); };
//      ws.logs      = []() -> String {           // historique (3 derniers)
//        String l = "  Logs: ";
//        for (int i = 0; i < wifiManager.getHistoryCount() && i < 3; i++) {
//          const auto* e = wifiManager.getHistoryEntry(i);
//          if (e && e->used) { l += e->timestamp; l += " "; l += e->ssid; l += " "; l += e->status; l += " "; }
//        }
//        return l;
//      };
//      sante.attacherWiFi(ws);
//    }
//
//    void loop() {
//      if (tacheBeat.TestDelay()) { tacheBeat.Reset(); Serial.println(sante.resume()); }
//      // API web :
//      // JsonDocument doc; sante.json(doc.as<JsonObject>()); serializeJson(doc, json);
//    }
//
//  Auteur : Olivier FOURNET — Licence GPL-3.0-only — org GitHub Fo170
// ============================================================================
#ifndef SANTE_IOT_H
#define SANTE_IOT_H


// Version de la librairie (source UNIQUE — agrégée par liste_lib.h)
#define __SANTE_IOT_H_VERSION__ "1.0.1"
#include <Arduino.h>

class Sante_iOT {
public:
  // --------------------------------------------------------------------------
  //  Identité
  // --------------------------------------------------------------------------
  void setNom(const char* nom)     { _nom = nom; }
  void setVersion(const char* v)   { _version = v; }

  // --------------------------------------------------------------------------
  //  Pointeurs sur variables applicatives (optionnels, nullptr = ignoré)
  // --------------------------------------------------------------------------
  void setUptimePtr(float* p)      { _uptime = p; }        // ex. &t_fct (s)
  void setRssiMoyPtr(int8_t* p)    { _rssi = p; }          // ex. &rssi_moy (dBm)
  void setHeapPtr(uint32_t* p)     { _heap = p; }          // ex. &heap_libre (octets)

  // --------------------------------------------------------------------------
  //  Attachement des compteurs ESP-NOW (pointeurs de fonction — découplé)
  // --------------------------------------------------------------------------
  struct EspNowSante {
    uint32_t (*rxTotal)() = nullptr;        // tous paquets reçus
    uint32_t (*rxCtrl)() = nullptr;         // paquets de contrôle traités
    uint32_t (*rxData)() = nullptr;         // données validées
    uint32_t (*rxJete)() = nullptr;         // données rejetées (mismatch)
    uint32_t (*rxOverflow)() = nullptr;     // trames écrasées (file pleine)
    uint8_t  (*nbPeers)() = nullptr;        // peers connus
    uint8_t  (*enLigne)() = nullptr;        // peers en ligne
    uint32_t (*derniereRecvMs)() = nullptr; // millis() de la dernière donnée reçue (0 = jamais)
  };
  void attacherEspNow(const EspNowSante& s) { _es = s; }

  // --------------------------------------------------------------------------
  //  Attachement des infos/logs WiFiManagerESP (v0.7.9+) — découplé
  //  Ex. : connecte=wifiManager.isConnected, ssid=wifiManager.getSSID,
  //  resets=wifiManager.getResetCount, dernierResetMs=wifiManager.getLastResetTime,
  //  logs = lambda qui formate l'historique (getHistoryEntry/getHistoryCount).
  // --------------------------------------------------------------------------
  struct WiFiSante {
    bool (*connecte)() = nullptr;                  // isConnected()
    String (*ssid)() = nullptr;                    // getSSID()
    int8_t (*rssi)() = nullptr;                    // getRSSI()
    uint32_t (*resets)() = nullptr;                // getResetCount() — chaque reset efface ESP-NOW
    unsigned long (*dernierResetMs)() = nullptr;   // getLastResetTime()
    String (*logs)() = nullptr;                    // formate les logs/historique (String)
  };
  void attacherWiFi(const WiFiSante& w) { _wifi = w; }

  // --------------------------------------------------------------------------
  //  Valeurs génériques (surveillance extensible, max SANTE_MAX_VALEURS)
  // --------------------------------------------------------------------------
  struct Valeur {
    const char* nom;
    float (*lire)();
    const char* unite;
  };
  bool ajouterValeur(const char* nom, float (*lire)(), const char* unite = "") {
    if (!nom || !lire || _nbValeurs >= SANTE_MAX_VALEURS) return false;
    _valeurs[_nbValeurs++] = { nom, lire, unite };
    return true;
  }

  // --------------------------------------------------------------------------
  //  Seuils (score / enSante)
  // --------------------------------------------------------------------------
  void setSeuilHeapMin(uint32_t b)      { _heapMin = b; }
  void setSeuilRssiMin(int8_t dBm)      { _rssiMin = dBm; }
  void setSeuilPerteMax(float pct)      { _perteMax = pct; }   // 0..1

  // --------------------------------------------------------------------------
  //  Métriques calculées
  // --------------------------------------------------------------------------

  /** Part de paquets non traités (perdus/débordés) : 0..1 (1 = tout perdu). */
  float tauxPerteRx() const {
    uint32_t total = _get(_es.rxTotal);
    if (total == 0) return 0.0f;
    uint32_t traites = _get(_es.rxCtrl) + _get(_es.rxData) + _get(_es.rxJete);
    float t = (total > traites) ? (float)(total - traites) / (float)total : 0.0f;
    float o = (float)_get(_es.rxOverflow) / (float)total;
    float p = t + o;
    return (p > 1.0f) ? 1.0f : p;
  }

  /** Validité des données reçues : 0..1 (1 = aucune rejetée). */
  float tauxValiditeDonnees() const {
    uint32_t data = _get(_es.rxData);
    uint32_t jete = _get(_es.rxJete);
    uint32_t tot = data + jete;
    return (tot == 0) ? 1.0f : (float)data / (float)tot;
  }

  /** Âge (s) de la dernière donnée reçue ; 0xFFFFFFFF si jamais reçue. */
  uint32_t ageDerniereRecv() const {
    if (!_es.derniereRecvMs) return 0xFFFFFFFF;
    uint32_t last = _es.derniereRecvMs();
    if (last == 0) return 0xFFFFFFFF;
    return (uint32_t)((millis() - last) / 1000);
  }

  /** Score global 0..100 : 100 = parfait. */
  uint8_t scoreGlobal() const {
    uint8_t s = 100;

    // RSSI
    if (_rssi && *_rssi < _rssiMin) {
      uint8_t d = (uint8_t)(_rssiMin - *_rssi);
      s = (d >= 20) ? 20 : s - d * 2;
    }
    // Heap
    if (_heap && *_heap < _heapMin) {
      s = 40;
    }
    // Perte RX ESP-NOW
    float p = tauxPerteRx();
    if (p > _perteMax) s = 30;
    else if (p > _perteMax * 0.5f) s = (uint8_t)(s - 20);
    // Validité données
    if (tauxValiditeDonnees() < 0.7f) s = (uint8_t)(s - 15);
    // Dernière réception
    uint32_t age = ageDerniereRecv();
    if (age == 0xFFFFFFFF) s = 20;
    else if (age > 120) s = 40;

    if (s > 100) s = 100;
    return s;
  }

  /** true si tous les critères de santé sont bons. */
  bool enSante() const {
    return scoreGlobal() >= 60;
  }

  // --------------------------------------------------------------------------
  //  Sorties
  // --------------------------------------------------------------------------

  /** Rapport texte multi-lignes (Telegram, /etat, MCP). */
  String rapport() const {
    String s = "\n=== SANTÉ iOT ===\n";
    s += _nom; s += " v"; s += _version; s += "\n";
    if (_uptime) { s += "Uptime: "; s += String(*_uptime, 1); s += " s\n"; }
    if (_rssi)   { s += "RSSI moy: "; s += String(*_rssi); s += " dBm\n"; }
    if (_heap)   { s += "Heap libre: "; s += String(*_heap); s += " B\n"; }

    // Bloc WiFi (infos/logs WiFiManagerESP)
    if (_wifi.connecte || _wifi.resets) {
      s += "[WiFi] ";
      if (_wifi.connecte) { s += _wifi.connecte() ? "connecté ✅" : "PAS_CONNECTÉ ❌"; }
      if (_wifi.ssid)     { s += " "; s += _wifi.ssid(); }
      if (_wifi.rssi)     { s += " RSSI="; s += String(_wifi.rssi()); s += " dBm"; }
      s += "\n";
      if (_wifi.resets) {
        s += "  resets WiFi="; s += String(_wifi.resets());
        if (_wifi.dernierResetMs) {
          unsigned long t = _wifi.dernierResetMs();
          s += " (dernier il y a "; s += String((millis() - t) / 1000); s += " s)";
        }
        s += "\n";
      }
      if (_wifi.logs) { s += _wifi.logs(); s += "\n"; }
    }

    // Bloc ESP-NOW
    uint32_t total = _get(_es.rxTotal);
    if (_es.rxTotal) {
      s += "[ESP-NOW] rx total="; s += String(total);
      s += " ctrl=";  s += String(_get(_es.rxCtrl));
      s += " data=";  s += String(_get(_es.rxData));
      s += " jeté=";  s += String(_get(_es.rxJete));
      s += " overflow="; s += String(_get(_es.rxOverflow));
      s += "\n";
      if (_es.nbPeers) {
        s += "  peers="; s += String(_get(_es.nbPeers));
        s += " en-ligne="; s += String(_get(_es.enLigne));
      }
      char buf[24];
      snprintf(buf, sizeof(buf), "%.0f%%", tauxPerteRx() * 100.0f);
      s += " | perte="; s += buf;
      snprintf(buf, sizeof(buf), "%.0f%%", tauxValiditeDonnees() * 100.0f);
      s += " validité="; s += buf;
      uint32_t age = ageDerniereRecv();
      s += " | dernière recv: ";
      s += (age == 0xFFFFFFFF) ? "jamais" : String(age) + " s";
      s += "\n";
    }

    // Valeurs génériques
    for (uint8_t i = 0; i < _nbValeurs; i++) {
      s += _valeurs[i].nom; s += ": ";
      s += String(_valeurs[i].lire(), 1);
      if (_valeurs[i].unite && _valeurs[i].unite[0]) { s += " "; s += _valeurs[i].unite; }
      s += "\n";
    }

    uint8_t sc = scoreGlobal();
    s += "Score santé: "; s += String(sc); s += "/100 (";
    s += (sc >= 60) ? "en santé" : "dégradé";
    s += ")\n";
    return s;
  }

  /** Résumé sur une ligne (heartbeat debug). */
  String resume() const {
    char buf[32];
    String s = _nom; s += " ";
    if (_uptime) { s += "up="; s += String(*_uptime, 0); s += "s "; }
    if (_rssi)   { s += "rssi="; s += String(*_rssi); s += "dBm "; }
    if (_wifi.connecte) { s += _wifi.connecte() ? "wifi=OK " : "wifi=KO "; }
    if (_heap)   { s += "heap="; s += String(*_heap); s += " "; }
    if (_es.rxTotal) {
      s += "espnow: data="; s += String(_get(_es.rxData));
      s += " jeté="; s += String(_get(_es.rxJete));
      s += " ovf="; s += String(_get(_es.rxOverflow));
      snprintf(buf, sizeof(buf), " perte=%.0f%%", tauxPerteRx() * 100.0f); s += buf;
      s += " peers="; s += String(_get(_es.enLigne)); s += "/"; s += String(_get(_es.nbPeers));
    }
    s += " score="; s += String(scoreGlobal()); s += "/100";
    return s;
  }

  /** JSON ArduinoJson v7 (JsonDocument / JsonObject / rvalue). */
  template <typename T>
  void json(T&& obj) const {
    obj["nom"] = _nom;
    obj["version"] = _version;
    if (_uptime) obj["uptime_s"] = *_uptime;
    if (_rssi)   obj["rssi_moy_dbm"] = *_rssi;
    if (_heap)   obj["heap_libre"] = *_heap;
    obj["score_sante"] = scoreGlobal();
    obj["en_sante"] = enSante();
    if (_wifi.connecte) obj["wifi_connecte"] = _wifi.connecte();
    if (_wifi.ssid)     obj["wifi_ssid"] = _wifi.ssid();
    if (_wifi.rssi)     obj["wifi_rssi"] = _wifi.rssi();
    if (_wifi.resets)   obj["wifi_resets"] = _wifi.resets();
    if (_wifi.dernierResetMs) obj["wifi_dernier_reset_ms"] = (uint32_t)_wifi.dernierResetMs();
    if (_es.rxTotal) {
      obj["espnow_total"] = _get(_es.rxTotal);
      obj["espnow_ctrl"] = _get(_es.rxCtrl);
      obj["espnow_data"] = _get(_es.rxData);
      obj["espnow_jete"] = _get(_es.rxJete);
      obj["espnow_overflow"] = _get(_es.rxOverflow);
      obj["espnow_perte"] = tauxPerteRx();
      obj["espnow_validite"] = tauxValiditeDonnees();
      uint32_t age = ageDerniereRecv();
      obj["espnow_derniere_recv_s"] = (age == 0xFFFFFFFF) ? -1 : (int32_t)age;
      if (_es.nbPeers) { obj["espnow_peers"] = _get(_es.nbPeers); obj["espnow_en_ligne"] = _get(_es.enLigne); }
    }
    for (uint8_t i = 0; i < _nbValeurs; i++) {
      obj[_valeurs[i].nom] = _valeurs[i].lire();
    }
  }

private:
  static uint32_t _get(uint32_t (*f)()) { return f ? f() : 0; }
  static uint8_t  _get(uint8_t (*f)())  { return f ? f() : 0; }

  const char* _nom = "";
  const char* _version = "";
  float* _uptime = nullptr;
  int8_t* _rssi = nullptr;
  uint32_t* _heap = nullptr;
  EspNowSante _es;
  WiFiSante _wifi;

  static const uint8_t SANTE_MAX_VALEURS = 8;
  Valeur _valeurs[SANTE_MAX_VALEURS];
  uint8_t _nbValeurs = 0;

  uint32_t _heapMin = 20000;
  int8_t _rssiMin = -85;
  float _perteMax = 0.50f;
};

// Instance globale — inclusion UNIQUE par projet (comme espNowTR_sharedVar)
Sante_iOT sante_iOT;

#endif // SANTE_IOT_H
