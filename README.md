# SAM-IOT-ML Zephyr Project

## Prerequisites

All build commands require the `BOARD_ROOT` parameter to point to this project directory:

```
-DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
```

## Build Commands

### feather_m4_zephyr_ot

```bash
west build -p always -b feather_m4_zephyr_ot mchp_6dof_imu -d build_mchp_6dof_imu -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b feather_m4_zephyr_ot mchp_hrm_gfx -d build_mchp_hrm_gfx -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b feather_m4_zephyr_ot mchp_hw_test -d build_mchp_hw_test -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b feather_m4_zephyr_ot mchp_range_gfx -d build_mchp_range_gfx -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b feather_m4_zephyr_ot mchp_weather_gfx -d build_mchp_weather_gfx -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
```

### same54_xpro

```bash
west build -p always -b same54_xpro mchp_6dof_imu -d build_mchp_6dof_imu -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b same54_xpro mchp_hrm_gfx -d build_mchp_hrm_gfx -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b same54_xpro mchp_hw_test -d build_mchp_hw_test -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b same54_xpro mchp_range_gfx -d build_mchp_range_gfx -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
west build -p always -b same54_xpro mchp_weather_gfx -d build_mchp_weather_gfx -- -DBOARD_ROOT="C:\Microchip\SAM-IOT-ML\zephyrproject"
```

## Flash Commands

### feather_m4_zephyr_ot

Uses the bossac runner. Adjust `--bossac-port` to match your COM port. To enter bootloader mode on the feather board you must press the reset button quickly twice.

```bash
west flash -r bossac --bossac-port="COM29" -d build_mchp_6dof_imu
west flash -r bossac --bossac-port="COM29" -d build_mchp_hrm_gfx
west flash -r bossac --bossac-port="COM29" -d build_mchp_hw_test
west flash -r bossac --bossac-port="COM29" -d build_mchp_range_gfx
west flash -r bossac --bossac-port="COM29" -d build_mchp_weather_gfx
```

### same54_xpro

```bash
west flash -d build_mchp_6dof_imu
west flash -d build_mchp_hrm_gfx
west flash -d build_mchp_hw_test
west flash -d build_mchp_range_gfx
west flash -d build_mchp_weather_gfx
```
