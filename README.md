# Модуль value для zephyr RTOS

Для применения в out-of-tree проектах:

```
west build -p always -b <board> app/<app> -DZEPHYR_EXTRA_MODULES="$(pwd)/value"
```
