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
#define __SANTE_IOT_H_VERSION__ "1.1.0"
#include <Arduino.h>

// ----------------------------------------------------------------------------
// StringPrint — adaptateur `Print` → `String`
//
// ⚠️ NÉCESSAIRE : en Arduino, `String` n'hérite PAS de `Print` (elle a
//    `concat()` mais pas `write()`). Sans cet adaptateur, les surcharges
//    `String` ne pourraient pas déléguer aux surcharges `Print&`, et la logique
//    de formatage devrait être écrite DEUX fois.
//    ESP8266 fournit `StringPrint` dans le core ; ESP32 non → définition locale
//    protégée par #ifndef pour ne pas entrer en collision sur ESP8266.
// ----------------------------------------------------------------------------
#ifndef SANTE_STRINGPRINT_DEFINI
#define SANTE_STRINGPRINT_DEFINI
class Sante_StringPrint : public Print {
public:
  explicit Sante_StringPrint(String& cible) : _s(cible) {}
  size_t write(uint8_t c) override { _s += (char)c; return 1; }
  size_t write(const uint8_t* d, size_t n) override {
    if (d && n) _s.concat((const char*)d, n);
    return n;
  }
private:
  String& _s;
};
#endif

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

  // ── Getters (v1.1.0-local) : alternative aux pointeurs sur globales ──────
  // ⚠️ POURQUOI : les pointeurs obligent le programme à maintenir une globale à
  //    jour (une écriture par boucle) pour une valeur qu'il sait déjà lire à la
  //    demande. Un getter évite cette globale ET la désynchronisation.
  //    Ex. : sante.setHeapGetter([]() -> uint32_t { return CM_Mesure().libre; });
  //    Priorité : getter > pointeur (si les deux sont fournis).
  void setHeapGetter(uint32_t (*f)()) { _heapGet = f; }
  void setRssiGetter(int8_t (*f)())   { _rssiGet = f; }
  void setUptimeGetter(float (*f)())  { _uptimeGet = f; }

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

  /** Âge (s) de la dernière donnée reçue ; 0xFFFFFFFF si jamais reçue.
   *  ⚠️ Vaut aussi 0xFFFFFFFF quand ESP-NOW n'est PAS attaché — c'est pourquoi
   *  `espNowAttache()` existe : les pénalités ESP-NOW ne doivent s'appliquer
   *  que si la source de données existe réellement. */
  uint32_t ageDerniereRecv() const {
    if (!_es.derniereRecvMs) return 0xFFFFFFFF;
    uint32_t last = _es.derniereRecvMs();
    if (last == 0) return 0xFFFFFFFF;
    return (uint32_t)((millis() - last) / 1000);
  }

  /** true si des compteurs ESP-NOW ont été attachés (au moins le compteur total). */
  bool espNowAttache() const { return _es.rxTotal != nullptr; }

  /** true si une source de « dernière réception » est attachée. */
  bool receptionAttachee() const { return _es.derniereRecvMs != nullptr; }

  // --------------------------------------------------------------------------
  //  Score de santé 0..100
  //
  //  ⚠️ DEUX DÉFAUTS CORRIGÉS ICI (v1.1.0-local, 25/10/2026) :
  //
  //  B1 — SOUS-DÉBORDEMENT uint8_t. L'ancien code faisait
  //       `s = (uint8_t)(s - 15);` : quand s valait déjà 5, s-15 donnait 246,
  //       puis `if (s > 100) s = 100;` RAMENAIT LE SCORE À 100. Un appareil
  //       très dégradé s'annonçait donc EN PARFAITE SANTÉ — le pire cas
  //       possible pour un indicateur de santé. Corrigé : arithmétique SIGNÉE
  //       (int16_t) + bornage bas, et aucune remontée possible.
  //
  //  B2 — ESP-NOW IMPOSÉ. `ageDerniereRecv()` renvoie 0xFFFFFFFF quand aucun
  //       compteur n'est attaché, et le score appliquait `s = 20`. Résultat :
  //       toute carte SANS ESP-NOW (ESP32_TELEGRAM, Thermometre, Relais…)
  //       plafonnait à 20/100 et `enSante()` était TOUJOURS faux. Corrigé :
  //       les pénalités ESP-NOW ne s'appliquent que si `espNowAttache()`.
  //
  //  La sémantique retenue est celle d'une NOTE : on part de 100 et on RETIRE
  //  des points. Aucun critère ne peut REMONTER le score (les anciennes
  //  affectations `s = 40` / `s = 30` écrasaient les pénalités précédentes).
  // --------------------------------------------------------------------------
  uint8_t scoreGlobal() const {
    int16_t s = 100;

    // ── WiFi : non connecté = pénalité majeure (rien ne fonctionne sans lui)
    if (_wifi.connecte && !_wifi.connecte()) s -= 40;

    // ── RSSI : −2 points par dB sous le seuil, plafonné à −40
    if (rssiPresente()) {
      int8_t r = rssiVal();
      if (r < _rssiMin) {
        int16_t d = (int16_t)(_rssiMin - r);
        s -= (d >= 20) ? 40 : (int16_t)(d * 2);
      }
    }

    // ── Heap : −60 sous le seuil (une carte sans heap ne peut plus servir)
    if (heapPresent()) {
      if (heapVal() < _heapMin) s -= 60;
    }

    // ── ESP-NOW : uniquement si la source existe (B2)
    if (espNowAttache()) {
      float p = tauxPerteRx();
      if (p > _perteMax)                    s -= 40;
      else if (p > _perteMax * 0.5f)        s -= 20;
      if (tauxValiditeDonnees() < 0.7f)     s -= 15;
      uint32_t age = ageDerniereRecv();
      if (age == 0xFFFFFFFF)                s -= 40;   // jamais reçu
      else if (age > 120)                   s -= 30;
    }

    if (s < 0)   s = 0;
    if (s > 100) s = 100;
    return (uint8_t)s;
  }

  /** true si tous les critères de santé sont bons. */
  bool enSante() const {
    return scoreGlobal() >= 60;
  }

  // --------------------------------------------------------------------------
  //  Sorties
  // --------------------------------------------------------------------------

  /** Rapport texte multi-lignes — version FLUX (v1.1.0-local).
   *  Aucune String allouée : l'appelant fournit Serial / le client HTTP.
   *  ⚠️ `Print` n'est pas un type Arduino pour ESP8266/ESP32 : c'est bien
   *  `Print` de `Print.h`, fourni par le core. Disponible sur les deux. */
  void rapport(Print& out) const {
    out.print(F("\n=== SANTÉ iOT ===\n"));
    out.print(_nom); out.print(F(" v")); out.print(_version); out.print('\n');
    if (uptimePresent()) { out.print(F("Uptime: ")); out.print((double)uptimeVal(), 1); out.print(F(" s\n")); }
    if (rssiPresente())  { out.print(F("RSSI moy: ")); out.print((int)rssiVal()); out.print(F(" dBm\n")); }
    if (heapPresent())   { out.print(F("Heap libre: ")); out.print((unsigned long)heapVal()); out.print(F(" B\n")); }

    // Bloc WiFi (infos/logs WiFiManagerESP)
    if (_wifi.connecte || _wifi.resets) {
      out.print(F("[WiFi] "));
      if (_wifi.connecte) { out.print(_wifi.connecte() ? F("connecté ✅") : F("PAS_CONNECTÉ ❌")); }
      if (_wifi.ssid)     { out.print(' '); out.print(_wifi.ssid()); }
      if (_wifi.rssi)     { out.print(F(" RSSI=")); out.print((int)_wifi.rssi()); out.print(F(" dBm")); }
      out.print('\n');
      if (_wifi.resets) {
        out.print(F("  resets WiFi=")); out.print((unsigned long)_wifi.resets());
        if (_wifi.dernierResetMs) {
          unsigned long t = _wifi.dernierResetMs();
          out.print(F(" (dernier il y a ")); out.print((unsigned long)((millis() - t) / 1000)); out.print(F(" s)"));
        }
        out.print('\n');
      }
      if (_wifi.logs) { out.print(_wifi.logs()); out.print('\n'); }
    }

    // Bloc ESP-NOW (seulement si attaché)
    if (espNowAttache()) {
      out.print(F("[ESP-NOW] rx total=")); out.print((unsigned long)_get(_es.rxTotal));
      out.print(F(" ctrl="));  out.print((unsigned long)_get(_es.rxCtrl));
      out.print(F(" data="));  out.print((unsigned long)_get(_es.rxData));
      out.print(F(" jeté="));  out.print((unsigned long)_get(_es.rxJete));
      out.print(F(" overflow=")); out.print((unsigned long)_get(_es.rxOverflow));
      out.print('\n');
      if (_es.nbPeers) {
        out.print(F("  peers=")); out.print((unsigned)_get(_es.nbPeers));
        out.print(F(" en-ligne=")); out.print((unsigned)_get(_es.enLigne));
      }
      char buf[24];
      snprintf(buf, sizeof(buf), " | perte=%.0f%%", tauxPerteRx() * 100.0f);
      out.print(buf);
      snprintf(buf, sizeof(buf), " validité=%.0f%%", tauxValiditeDonnees() * 100.0f);
      out.print(buf);
      uint32_t age = ageDerniereRecv();
      out.print(F(" | dernière recv: "));
      if (age == 0xFFFFFFFF) out.print(F("jamais"));
      else { out.print((unsigned long)age); out.print(F(" s")); }
      out.print('\n');
    }

    // Valeurs génériques
    for (uint8_t i = 0; i < _nbValeurs; i++) {
      out.print(_valeurs[i].nom); out.print(F(": "));
      out.print((double)_valeurs[i].lire(), 1);
      if (_valeurs[i].unite && _valeurs[i].unite[0]) { out.print(' '); out.print(_valeurs[i].unite); }
      out.print('\n');
    }

    uint8_t sc = scoreGlobal();
    out.print(F("Score santé: ")); out.print((unsigned)sc); out.print(F("/100 ("));
    out.print((sc >= 60) ? F("en santé") : F("dégradé"));
    out.print(F(")\n"));
  }

  /** Rapport texte multi-lignes (valeur) — enveloppe de la version flux. */
  String rapport() const {
    String s;
    Sante_StringPrint sp(s);
    rapport(sp);
    return s;
  }

  /** Résumé sur une ligne — version FLUX (v1.1.0-local). Aucune String allouée. */
  void resume(Print& out) const {
    char buf[40];
    out.print(_nom); out.print(' ');
    if (uptimePresent()) { out.print(F("up=")); out.print((double)uptimeVal(), 0); out.print(F("s ")); }
    if (rssiPresente())  { out.print(F("rssi=")); out.print((int)rssiVal()); out.print(F("dBm ")); }
    if (_wifi.connecte) { out.print(_wifi.connecte() ? F("wifi=OK ") : F("wifi=KO ")); }
    if (heapPresent())   { out.print(F("heap=")); out.print((unsigned long)heapVal()); out.print(' '); }
    if (espNowAttache()) {
      out.print(F("espnow: data=")); out.print((unsigned long)_get(_es.rxData));
      out.print(F(" jeté=")); out.print((unsigned long)_get(_es.rxJete));
      out.print(F(" ovf=")); out.print((unsigned long)_get(_es.rxOverflow));
      snprintf(buf, sizeof(buf), " perte=%.0f%%", tauxPerteRx() * 100.0f);
      out.print(buf);
      out.print(F(" peers=")); out.print((unsigned)_get(_es.enLigne));
      out.print('/'); out.print((unsigned)_get(_es.nbPeers));
    }
    out.print(F(" score=")); out.print((unsigned)scoreGlobal()); out.print(F("/100"));
  }

  /** Résumé sur une ligne (valeur) — enveloppe de la version flux. */
  String resume() const {
    String s;
    Sante_StringPrint sp(s);
    resume(sp);
    return s;
  }

  /** JSON ArduinoJson v7 (JsonDocument / JsonObject / rvalue). */
  template <typename T>
  void json(T&& obj) const {
    obj["nom"] = _nom;
    obj["version"] = _version;
    if (uptimePresent()) obj["uptime_s"] = uptimeVal();
    if (rssiPresente())  obj["rssi_moy_dbm"] = rssiVal();
    if (heapPresent())   obj["heap_libre"] = heapVal();
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

  // ── Accès unifié getter > pointeur (v1.1.0-local) ──────────────────────
  bool heapPresent() const   { return _heapGet != nullptr || _heap != nullptr; }
  bool rssiPresente() const  { return _rssiGet != nullptr || _rssi != nullptr; }
  bool uptimePresent() const { return _uptimeGet != nullptr || _uptime != nullptr; }
  uint32_t heapVal() const   { return _heapGet ? _heapGet() : (_heap ? *_heap : 0); }
  int8_t   rssiVal() const   { return _rssiGet ? _rssiGet() : (_rssi ? *_rssi : 0); }
  float    uptimeVal() const { return _uptimeGet ? _uptimeGet() : (_uptime ? *_uptime : 0.0f); }

  const char* _nom = "";
  const char* _version = "";
  float* _uptime = nullptr;
  int8_t* _rssi = nullptr;
  uint32_t* _heap = nullptr;
  float    (*_uptimeGet)() = nullptr;
  int8_t   (*_rssiGet)()   = nullptr;
  uint32_t (*_heapGet)()   = nullptr;
  EspNowSante _es;
  WiFiSante _wifi;

  static const uint8_t SANTE_MAX_VALEURS = 8;
  Valeur _valeurs[SANTE_MAX_VALEURS];
  uint8_t _nbValeurs = 0;

  uint32_t _heapMin = 20000;
  int8_t _rssiMin = -85;
  float _perteMax = 0.50f;
};

// ⚠️ B3 — L'INSTANCE GLOBALE `Sante_iOT sante_iOT;` A ÉTÉ RETIRÉE (v1.1.0-local).
//    Déclarée en fin d'en-tête SANS `inline`, elle produisait une définition par
//    unité de compilation qui inclut le header → erreur de linkage dès que le
//    projet a deux .cpp (ou un .ino + un .cpp). `inline` n'est pas une option :
//    le core arduino-esp32 2.0.17 compile en `gnu++11`, où les variables
//    `inline` (C++17) n'existent pas.
//    → Déclarer l'instance dans le programme : `Sante_iOT sante;`

#endif // SANTE_IOT_H
