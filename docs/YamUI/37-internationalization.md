# X — YamUI Internationalization (i18n) System  
**Multilingual Text, Translation Bundles, Locale Switching, and Embedded-Friendly Formatting**

This document defines the **YamUI Internationalization (i18n) System**, which enables YamUI applications to support multiple languages, locale-aware formatting, and dynamic text substitution — all while remaining lightweight and embedded-friendly.

The i18n system provides:

- translation bundles  
- locale selection  
- dynamic language switching  
- parameterized strings  
- pluralization rules (optional extension)  
- date/number formatting (optional extension)  

This system ensures YamUI UIs can be deployed globally without duplicating screens or components.

---

# 1. Overview

YamUI supports internationalization through:

1. **Translation bundles** — key/value dictionaries  
2. **Locale selection** — choose active language  
3. **Translation lookup** — `t("key")`  
4. **Parameterized strings** — `t("key", { param: value })`  
5. **Dynamic switching** — update UI when locale changes  

Translations are stored in YAML and bundled with the UI.

---

# 2. Translation Bundle Structure

Translation bundles live under:

```
/src/ui/i18n
```

### Example directory

```
i18n/en.yaml
i18n/es.yaml
i18n/de.yaml
```

### Example translation file (`en.yaml`)

```yaml
i18n:
  wifi.setup: "WiFi Setup"
  wifi.scan: "Scan Networks"
  wifi.password: "Password"
  wifi.connecting: "Connecting to {{ssid}}..."
```

Keys use dot-notation for organization.

---

# 3. Referencing Translations in YAML

Translations are referenced using the `t()` function.

### Example

```yaml
- type: label
  text: "{{ t('wifi.setup') }}"
```

### With parameters

```yaml
text: "{{ t('wifi.connecting', { ssid: wifi.ssid }) }}"
```

### With fallback

```yaml
text: "{{ t('wifi.unknown', {}, 'Unknown') }}"
```

---

# 4. Locale Selection

The active locale is defined in `app:`:

```yaml
app:
  locale: "en"
```

Or changed at runtime:

```yaml
on_click: set(app.locale, "es")
```

Changing the locale triggers:

- re-evaluation of all `t()` expressions  
- reactive UI updates  

---

# 5. Translation Lookup Rules

When evaluating `t("key")`:

1. Look up key in active locale  
2. If missing, fall back to default locale  
3. If still missing, return key or fallback string  

### Example

```
t("wifi.setup") → "WiFi Setup"
```

### Missing key

```
t("wifi.missing") → "wifi.missing"
```

Unless fallback is provided:

```
t("wifi.missing", {}, "Unknown") → "Unknown"
```

---

# 6. Parameterized Strings

Translations may include placeholders:

```yaml
wifi.connecting: "Connecting to {{ssid}}..."
```

Usage:

```yaml
text: "{{ t('wifi.connecting', { ssid: wifi.ssid }) }}"
```

Placeholders support:

- strings  
- numbers  
- booleans  
- expressions  

---

# 7. Pluralization (Optional Extension)

YamUI supports simple pluralization rules:

### Example translation file

```yaml
wifi.networks:
  one: "1 network found"
  other: "{{count}} networks found"
```

### Usage

```yaml
text: "{{ t('wifi.networks', { count: wifi.count }) }}"
```

Plural category is selected based on locale rules.

---

# 8. Locale-Aware Formatting (Optional Extension)

YamUI can format:

- numbers  
- dates  
- times  

### Example

```yaml
text: "{{ format_number(sensor.value, 'de') }}"
```

### Date formatting

```yaml
text: "{{ format_date(ui.timestamp, 'short') }}"
```

Formatting rules follow the active locale unless overridden.

---

# 9. Runtime Locale Switching

Changing locale at runtime:

```yaml
on_click: set(app.locale, "de")
```

Triggers:

- translation cache invalidation  
- re-evaluation of all `t()` expressions  
- UI updates  

No screens or components are re-rendered.

---

# 10. i18n in Components

Components can use translations normally:

```yaml
components:
  WifiCard:
    widgets:
      - type: label
        text: "{{ t('wifi.ssid') }}: {{ssid}}"
```

Props and translations can be mixed freely.

---

# 11. i18n in Screens

Screens can use translations for:

- titles  
- buttons  
- messages  
- dynamic content  

### Example

```yaml
screens:
  wifi_setup:
    widgets:
      - type: label
        text: "{{ t('wifi.setup') }}"
      - type: button
        text: "{{ t('wifi.scan') }}"
```

---

# 12. i18n Build Pipeline Integration

During the build pipeline (Section P):

1. All translation files are collected  
2. Keys are validated  
3. Missing keys across locales are reported  
4. Bundles are merged into the UI package  
5. Translation index is generated  

### Example warning

```
[i18n] Missing key 'wifi.scan' in locale 'de'
```

---

# 13. Performance Considerations

### 13.1 Translation Lookup

- O(1) hash lookup  
- cached per expression  
- extremely fast  

### 13.2 Locale Switching

- re-evaluates only `t()` expressions  
- no LVGL object churn  
- minimal CPU usage  

### 13.3 Bundle Size

Translation files are small:

- English only: ~1–3 KB  
- 3–5 languages: ~5–15 KB  

---

# 14. Example: Full i18n Setup

### `/i18n/en.yaml`

```yaml
i18n:
  wifi.setup: "WiFi Setup"
  wifi.scan: "Scan Networks"
```

### `/i18n/es.yaml`

```yaml
i18n:
  wifi.setup: "Configuración WiFi"
  wifi.scan: "Buscar redes"
```

### YAML usage

```yaml
text: "{{ t('wifi.scan') }}"
```

### Runtime locale switch

```yaml
on_click: set(app.locale, "es")
```

---

# 15. Summary

The YamUI Internationalization (i18n) System provides:

- translation bundles  
- locale selection  
- dynamic language switching  
- parameterized strings  
- pluralization rules  
- locale-aware formatting  
- embedded-friendly performance  

This system enables YamUI applications to support **global, multilingual deployments** without duplicating UI logic or screens.
