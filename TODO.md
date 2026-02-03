# TODO

- [ ] Update ESP-Hosted coprocessor to firmware 2.11.0 to clear version mismatch warning.
- [x] Implement LVGL display and touch drivers that plug into kc_touch_gui.
- [ ] Disable CONFIG_KC_TOUCH_RESET_PROVISIONED when persistent credentials are desired again.
- [x] Migrate wifi_copro_power I2C helper to the new driver/i2c_master API.
- [ ] Revisit partition table/header to use the full 16 MB flash capacity.
