# Noise Explorer

<p align="center">
<a href="https://asciinema.org/a/468494"><img src="https://asciinema.org/a/468494.svg" alt="ne_example" width="600"/></a>
</p>

Noise Explorer tui for [Auburn's FastNoiseLite](https://github.com/Auburn/FastNoiseLite/).
- [stb image](https://github.com/nothings/stb) is included.

### Building

- Requires [z_](https://github.com/zakarouf/z_) & [zsf](https://github.com/zakarouf/zsf)
```sh
gcc -Wall -O3 -lzkcollection -lzkzsf -fopenmp src/main.c -o ne
```

### Commands

```sh
./ne --help
Noise Explorer by zakarouf 2022 - 2023
Powered by nothings/stb & Auburn/FastNoiseLite
-w     --witdh [N]         Set Width
-h     --height [N]        Set Height
-x     --startx [N]        Set x start cord
-y     --starty [N]        Set y start cord

-nt     --noise_type [S]   Set noise
           | perlin
           | os2
           | os2s
           | cell
           | val
           | valc

-ns    --noise_seed [N]    Set Noise Seed
-nf    --noise_freq [F]    Set Noise Frequency,
                             [F] must be between 0.0 and 1.0
-no    --noise_oct [N]     Set Noise Ocatave
-ng    --noise_gain [F]    Set Noise Gain

--cellj [N]                Set Cellular Jitter Mod
--domamp  [N]              Set Domain Wrap Amplifier

-r     --write [S]         Write to a named[S] file

-f     --file [S]              Read Color and Char format from file
-cc    --charlist [S]          Get Character maplist, lower to higher
--cp [r,g,b] [r,g,b]           Push Color, disables auto-gen colors
--clp [S] [N] [N] [r,g,b]x4    Push a generated color map

-np    --noprint           Toggle oof terminal print
-d     --draw [S]          Set in Draw Mode/Method
           | char          Only Characters, Colorless
           | obg           Only Background Color
-e                         Start In Explorer Mode

```
