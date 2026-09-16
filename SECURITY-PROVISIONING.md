# Sécurisation et premier provisionnement

## Premier démarrage avec une NVS vierge

1. Flasher le firmware et SPIFFS par le port série.
2. Ouvrir le moniteur série à 115200 bauds.
3. Relever les trois lignes suivantes :

   - `SETUP WIFI SSID: TWC-Setup-XXXXXX`
   - `SETUP WIFI PASSWORD (serial only): ...`
   - `ADMIN TOKEN (serial only): ...`

4. Se connecter au réseau WPA2 `TWC-Setup-XXXXXX`.
5. Ouvrir `http://192.168.4.1/`.
6. Saisir le jeton administrateur de 32 caractères hexadécimaux.
7. Choisir le réseau Wi-Fi de production et enregistrer ses identifiants.

Le contrôleur coupe alors le point d'accès de provisionnement et démarre en
mode station avec les valeurs conservées dans NVS.

## Authentification HTTP

Toutes les routes `/api/*`, y compris la configuration, le redémarrage, les
journaux et l'OTA, exigent cet en-tête :

```text
Authorization: Bearer <jeton-administrateur>
```

L'interface Web conserve le jeton uniquement dans `sessionStorage`. Il est
supprimé à la fermeture de l'onglet et après une réponse HTTP 401.

## Vérifications après compilation

```powershell
idf.py fullclean
idf.py build
idf.py -p COM4 flash monitor
```

Tester ensuite sous PowerShell, en remplaçant les valeurs d'exemple :

```powershell
$ip = "192.168.4.1"
$token = "JETON_A_32_CARACTERES"

# Doit répondre 401.
curl.exe -i "http://$ip/api/config"

# Doit répondre 200 et ne jamais retourner le mot de passe Wi-Fi.
curl.exe -i -H "Authorization: Bearer $token" "http://$ip/api/config"
```

## Limites de sécurité restantes

Le serveur embarqué utilise encore HTTP. Le jeton ne doit donc traverser qu'un
réseau de gestion maîtrisé. Pour un produit exposé à un réseau non fiable,
ajouter HTTPS. Pour protéger les secrets présents dans NVS contre une lecture
physique de la flash, prévoir également Secure Boot et Flash Encryption dans
la procédure d'industrialisation ; ces options ne doivent pas être activées
automatiquement pendant le prototypage car elles programment des eFuses.
