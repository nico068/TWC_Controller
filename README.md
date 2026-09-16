# TWC Controller (ESP32)

Firmware ESP-IDF pour piloter un environnement Tesla Wall Connector Gen 3 via émulation Neurio Modbus RTU sur RS485.

Le projet combine :
- acquisition des mesures électriques via Shelly ;
- calcul de marge de puissance côté contrôleur ;
- émulation d'un compteur compatible pour la borne ;
- interface Web locale pour supervision et configuration.

## Etat du projet

- Version applicative : 0.1.0
- Cible : ESP32
- Build : CMake + ESP-IDF (projet minimal)
- Partitionnement : OTA dual-slot + SPIFFS

## Fonctionnalités principales

- Emulation Neurio Modbus RTU pour Tesla Wall Connector Gen 3   avec fimware 26.26
- Acquisition de mesures depuis Shelly (plusieurs parseurs pris en charge)
- API HTTP locale pour statut, configuration, santé, logs, OTA et outils système
- Interface Web embarquée (dossier www)
- Provisionnement sécurisé au premier démarrage (AP WPA2 + jeton admin)
- Persistance de configuration en NVS

## Architecture

Composants clés :
- components/config : configuration système et paramètres persistants
- components/storage : stockage NVS
- components/wifi_manager : gestion Wi-Fi (station + setup)
- components/shelly : découverte et collecte des mesures Shelly
- components/power_manager : calculs de marge et logique énergie
- components/rs485 : pile RS485 et émulation Modbus
- components/webserver : routes API et serveur HTTP
- components/scheduler : orchestration périodique
- components/watchdog : supervision de robustesse
- main/app_main.c : séquence d'initialisation globale

## Prérequis

- ESP-IDF 5.5.x
- Python et toolchain ESP-IDF installés
- Carte ESP32 compatible
- Adaptateur série pour flash/monitor

## Compilation et flash

Depuis la racine du dépôt, exécuter :

    idf.py fullclean
    idf.py build
    idf.py -p COM4 flash monitor

Adapter COM4 selon votre port série.

## Premier démarrage et sécurité

Sur une NVS vierge, l'app démarre en mode provisionnement et affiche sur la console série :
- SSID setup
- mot de passe du point d'accès
- jeton administrateur

Ensuite :
1. Se connecter au réseau setup.
2. Ouvrir http://192.168.4.1/
3. Saisir le jeton administrateur.
4. Enregistrer les identifiants Wi-Fi cibles.

Toutes les routes API sous /api exigent un header Authorization: Bearer <token>.

Voir aussi :
- SECURITY-PROVISIONING.md
- AUDIT.md

## API (aperçu)

Exemples de routes présentes dans le composant webserver :
- /api/status
- /api/system
- /api/health
- /api/config
- /api/logs
- /api/reboot
- /api/ota
- /api/shelly

## Structure du dépôt

- main : point d'entrée firmware
- components : logique applicative modulaire
- managed_components : dépendances gérées ESP-IDF
- www : frontend embarqué
- partitions.csv : table de partitions
- sdkconfig / sdkconfig.defaults : configuration build

## Notes importantes

- Le dossier build peut être volumineux ; il est généralement ignoré dans les flux Git classiques.
- Le serveur embarqué reste en HTTP local ; pour un réseau non maîtrisé, prévoir un durcissement réseau et chiffrement adapté.

## Licence

Ajouter ici la licence du projet (MIT, Apache-2.0, propriétaire, etc.).
