# MegaWiFi SGDK builds (C3 cart)

> Current working setup uses the rosco m68k toolchain at `/Users/MWOLAK/homebrew/Cellar/rosco-m68k-toolchain@13/20241103161658/bin/m68k-elf-rosco-` with SGDK at `~/sgdk`. See `BUILD_NOTES.md` for the repeatable steps. The legacy marsdev-specific commands below are kept for reference only.

Toolchain
- m68k: /Users/MWOLAK/homebrew/Cellar/rosco-m68k-toolchain@13/20241103161658/bin/m68k-elf-rosco-
- SGDK: ~/.marsdev/m68k-elf (built via marsdev; contains ext/mw v1.5 API)
- FINNHUB token: stored in TOKEN.md (auto-injected at build)

Build commands
- Stock ticker: rm -f out/*.o out/*.bin out/*.out
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DFINNHUB_TOKEN=\"d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30\" -c /Users/MWOLAK/.marsdev/m68k-elf/src/boot/rom_header.c -o out/rom_header.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-objcopy -O binary out/rom_header.o out/rom_header.bin
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DFINNHUB_TOKEN=\"d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30\" -x assembler-with-cpp -Wa,--register-prefix-optional,--bitwise-or -c /Users/MWOLAK/.marsdev/m68k-elf/src/boot/sega.s -o out/sega.o
Build: 72
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DFINNHUB_TOKEN=\"d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30\" -DBUILD_NUM=$(cat out/build_num) -c src/stock_ticker.c -o out/stock_ticker.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DFINNHUB_TOKEN=\"d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30\" -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/megawifi.c -o out/megawifi.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DFINNHUB_TOKEN=\"d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30\" -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/lsd.c -o out/lsd.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DFINNHUB_TOKEN=\"d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30\" -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/16c550.c -o out/16c550.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -DMODULE_MEGAWIFI=1 -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DFINNHUB_TOKEN=\"d6p5qm1r01qk3chj1a2gd6p5qm1r01qk3chj1a30\" -Wno-unused-function -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/json.c -o out/json.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -m68000 -n -T /Users/MWOLAK/.marsdev/m68k-elf/md.ld -nostdlib -fno-lto -Wl,--gc-sections out/sega.o out/stock_ticker.o out/megawifi.o out/lsd.o out/16c550.o out/json.o /Users/MWOLAK/.marsdev/m68k-elf/lib/libmd.a /Users/MWOLAK/.marsdev/m68k-elf/bin/../lib/gcc/m68k-elf/15.2.0/libgcc.a -o out/stock_ticker.out
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-objcopy -O binary out/stock_ticker.out out/stock_ticker.bin
ROM: out/stock_ticker.bin
  - Output: /Users/MWOLAK/MegaWifi/stock_ticker/out/stock_ticker.bin
- NTP app: rm -f out/*.o out/*.bin out/*.out
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -I/Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -c /Users/MWOLAK/.marsdev/m68k-elf/src/boot/rom_header.c -o out/rom_header.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-objcopy -O binary out/rom_header.o out/rom_header.bin
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -I/Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -x assembler-with-cpp -Wa,--register-prefix-optional,--bitwise-or -c /Users/MWOLAK/.marsdev/m68k-elf/src/boot/sega.s -o out/sega.o
Build: 2
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -I/Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -DBUILD_NUM=$(cat out/build_num) -c src/megawifi_ntp.c -o out/megawifi_ntp.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -I/Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/megawifi.c -o out/megawifi.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -I/Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/lsd.c -o out/lsd.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -I/Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/16c550.c -o out/16c550.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -DSGDK_GCC -m68000 -O2 -fomit-frame-pointer -Wall -Wextra -Wno-shift-negative-value -Wno-main -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions -I/Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw -Isrc -I/Users/MWOLAK/.marsdev/m68k-elf/inc -I/Users/MWOLAK/.marsdev/m68k-elf/res -Wno-unused-function -c /Users/MWOLAK/.marsdev/m68k-elf/src/ext/mw/json.c -o out/json.o
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-gcc -m68000 -n -T /Users/MWOLAK/.marsdev/m68k-elf/md.ld -nostdlib -fno-lto -Wl,--gc-sections out/sega.o out/megawifi_ntp.o out/megawifi.o out/lsd.o out/16c550.o out/json.o /Users/MWOLAK/.marsdev/m68k-elf/lib/libmd.a /Users/MWOLAK/.marsdev/m68k-elf/bin/../lib/gcc/m68k-elf/15.2.0/libgcc.a -o out/megawifi_ntp.out
/Users/MWOLAK/.marsdev/m68k-elf/bin/m68k-elf-objcopy -O binary out/megawifi_ntp.out out/megawifi_ntp.bin
ROM: out/megawifi_ntp.bin
  - Output: /Users/MWOLAK/MegaWifi/megawifi_ntp/out/megawifi_ntp.bin

Notes
- Both projects build against SGDK ext/mw (same API as fw v1.5.1) and expect the ESP32-C3 cart firmware v1.5.1 already flashed.
- The mw-api hello-wifi demo still needs fixes to compile cleanly with the rosco toolchain + SGDK headers (conflicts around SGDK string/memory prototypes). If you want, I can finish a minimal HTTPS demo using the same stock_ticker/NTP build system instead.


codex resume 019ce9cb-b04f-7190-9d94-838f28ca0e3d
