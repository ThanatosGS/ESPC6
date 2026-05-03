# XIAO ESP32-C6 — SSD1306 OLED Display

Projet ESP-IDF affichant du texte sur un écran OLED SSD1306 128×64 (I2C) branché sur un Seeed Studio XIAO ESP32-C6.

## Matériel requis

| Composant | Référence |
|-----------|-----------|
| Microcontrôleur | Seeed Studio XIAO ESP32-C6 |
| Écran | OLED SSD1306 0.96" 128×64 I2C |

## Câblage

> **Note :** La broche D5 (GPIO23) du XIAO ESP32-C6 est inutilisable en I2C car GPIO23 est
> lié au contrôleur SDIO et dispose d'un pull-down hardware. Utiliser D8 (GPIO19) pour SCL.

| OLED | XIAO ESP32-C6 | GPIO |
|------|---------------|------|
| VCC  | 3V3           | —    |
| GND  | GND           | —    |
| SDA  | D4            | GPIO22 |
| SCL  | **D8**        | GPIO19 |

## Structure du projet

```
├── CMakeLists.txt
├── sdkconfig.defaults       # cible esp32c6
└── main/
    ├── CMakeLists.txt
    ├── main.c               # point d'entrée, démo affichage
    ├── ssd1306.h            # API publique du driver
    └── ssd1306.c            # driver I2C + framebuffer + font 5×7
```

### API du driver SSD1306

```c
// Initialisation
esp_err_t ssd1306_init(ssd1306_t *oled, int sda, int scl, uint8_t addr);

// Effacer le framebuffer
void ssd1306_clear(ssd1306_t *oled);

// Envoyer le framebuffer vers l'écran
void ssd1306_flush(ssd1306_t *oled);

// Dessiner un pixel (on=1 allumé, on=0 éteint)
void ssd1306_set_pixel(ssd1306_t *oled, int x, int y, int on);

// Afficher un caractère (x en pixels, page = ligne 0-7)
void ssd1306_draw_char(ssd1306_t *oled, int x, int page, char c);

// Afficher une chaîne
void ssd1306_draw_string(ssd1306_t *oled, int x, int page, const char *s);

// Dessiner un rectangle vide
void ssd1306_draw_rect(ssd1306_t *oled, int x, int y, int w, int h);
```

**Repères utiles :**
- L'écran est divisé en 8 pages (lignes) de 8 pixels de haut chacune.
- Une chaîne avec la font 5×7 prend 6 pixels de large par caractère → 21 caractères max par ligne.
- Appeler `ssd1306_flush()` après chaque modification pour mettre à jour l'affichage.

## Prérequis

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/index.html) installé et configuré
- Python 3.x
- Pilote USB (CH340 ou similaire selon la version de la carte)

## Compilation et flash

```powershell
# 1. Configurer la cible (à faire une seule fois)
idf.py set-target esp32c6

# 2. Compiler
idf.py build

# 3. Flasher (remplacer COM12 par le bon port)
idf.py -p COM12 flash

# 4. Compiler, flasher et ouvrir le moniteur série en une commande
idf.py -p COM12 flash monitor
```

Pour quitter le moniteur série : `Ctrl + ]`

## Trouver le port COM

**Windows :**
```powershell
# Lister les ports série disponibles
[System.IO.Ports.SerialPort]::getportnames()
```
Ou dans le Gestionnaire de périphériques → Ports (COM et LPT).

**Linux / macOS :**
```bash
ls /dev/tty*   # Linux : /dev/ttyUSB0 ou /dev/ttyACM0
ls /dev/cu.*   # macOS  : /dev/cu.usbserial-...
```

## Personnalisation

Modifier les constantes dans `main/main.c` :

```c
#define OLED_SDA  22      // GPIO SDA
#define OLED_SCL  19      // GPIO SCL
#define OLED_ADDR 0x3C    // Adresse I2C (0x3C ou 0x3D)
```

Pour changer la vitesse I2C (défaut 100 kHz), modifier dans `main/ssd1306.c` :

```c
.scl_speed_hz = 100000,   // 100 kHz — augmenter à 400000 si pull-ups externes
```
