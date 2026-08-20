# Use the Waveshare BSP and enable touch navigation

The firmware will pin Waveshare's managed
`esp32_s3_touch_lcd_1_69` BSP and use LVGL through Espressif's LVGL port behind
a project-owned board adapter instead of copying vendor demo drivers. The touch
controller remains enabled for status navigation, detail expansion,
provisioning display, and brightness controls, but AP Configuration changes
remain exclusive to the Management Interface and Management API in the first
release.
