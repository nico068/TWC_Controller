# Audit du projet TWC Controller Gen 3

## Périmètre validé

Le chemin actif du projet est désormais unique :

1. le Shelly fournit les mesures de courant et de puissance ;
2. `power_manager` calcule la marge disponible pour l'interface et les diagnostics ;
3. `rs485` émule un compteur Neurio Modbus RTU pour le Wall Connector Gen 3 ;
4. le statut de la borne dépend de requêtes Modbus valides et récentes.

Le protocole Tesla Gen 2, SLIP et le gestionnaire de charge qui dépendait de cet ancien protocole ont été supprimés.

## Suppressions

- `components/twc_protocol/`
- `components/charge_manager/`
- `components/slip/`
- `components/event_manager/`
- `components/ota/` vide
- `components/rs485/rs485.c.sauvegarde`
- anciennes API RS485 Gen 2 et files FreeRTOS associées
- faux états Gen 2 exposés par les API
- fonctions publiques sans appel dans le logger, le stockage et l'écrivain JSON

## Corrections

- suppression de la dépendance CMake circulaire entre `config` et `storage` ;
- création explicite de la boucle d'événements ESP-IDF avant l'initialisation Wi-Fi ;
- démarrage immédiat de la tâche Shelly et nouvelle tentative automatique de détection après une panne Wi-Fi au démarrage ;
- redémarrage de la détection lors d'un changement d'adresse Shelly ;
- invalidation immédiate de la limite calculée lors d'un changement de phase ;
- `/api/status`, `/api/system` et `/api/health` utilisent le statut Modbus Gen 3 réel ;
- suppression de l'ancienne adresse Shelly de secours codée en dur ;
- réduction du journal circulaire de 256 à 64 entrées pour libérer environ 30 Kio de DRAM.
- suppression des identifiants Wi-Fi compilés dans le firmware ;
- point d'accès WPA2 de provisionnement au premier démarrage, avec mot de passe aléatoire affiché uniquement sur la console série ;
- authentification de toutes les API par un jeton administrateur aléatoire et persistant ;
- réutilisation du jeton administrateur pour l'OTA à la place d'un second secret.

## Éléments conservés volontairement

- le registre Modbus et les réponses qui ont permis la détection du compteur par la borne ;
- les parseurs Shelly Gen1, Gen2 V1 et Gen2 V2 ;
- les fonctions d'arrêt des composants actifs, même si le démarrage normal ne les appelle pas ;
- la configuration de partition OTA avec deux emplacements applicatifs et SPIFFS.

## Contrôles effectués

- aucune référence restante aux composants Gen 2, SLIP, `charge_manager` ou `event_manager` ;
- aucune sauvegarde source ou sortie de compilation dans le projet ;
- graphe des dépendances locales CMake sans cycle ;
- partition 4 Mio cohérente : NVS, OTA data, deux applications de 1,5 Mio et SPIFFS ;
- rollback OTA activé ;
- console UART0 distincte de l'UART2 RS485.

## Blocages avant une utilisation de production

- la charge réelle n'a pas encore été testée ; le projet ne peut pas être déclaré sûr en production avant ce test ;
- le Shelly est interrogé en HTTP non chiffré ;
- le serveur d'administration utilise encore HTTP : réserver l'accès à un VLAN de gestion fiable ou migrer vers HTTPS avant exposition à un réseau non maîtrisé ;
- activer Secure Boot et Flash Encryption lors de l'industrialisation afin de protéger les secrets conservés en NVS ;
- la compilation ESP-IDF doit être exécutée sur l'environnement Windows du projet, ESP-IDF 5.5.1 n'étant pas installé dans l'environnement de cet audit.

## Validation à exécuter sous Windows

```powershell
idf.py fullclean
idf.py build
idf.py -p COM4 flash monitor
```

Après démarrage, vérifier au minimum :

- `Shelly measurement is valid` ;
- `GEN3 Modbus/5s` avec `requests` et `responses` non nuls et égaux ;
- le statut Web de la borne passe à connectée ;
- une coupure puis un retour du Wi-Fi restaure automatiquement les mesures Shelly ;
- une coupure puis un retour du RS485 restaure automatiquement le statut de la borne.

Sur une NVS vierge, vérifier également :

- le réseau `TWC-Setup-XXXXXX` apparaît ;
- son mot de passe et le jeton `ADMIN TOKEN` apparaissent uniquement sur la console série ;
- `http://192.168.4.1/` permet de saisir le jeton puis d'enregistrer le Wi-Fi ;
- un appel à `/api/config` sans en-tête `Authorization: Bearer ...` reçoit HTTP 401.
