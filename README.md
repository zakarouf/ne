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

```rust
>>> ne --help
Noise Explorer by zakarouf 2022 - 2023
Powered by nothings/stb & Auburn/FastNoiseLite

COMMANDS:
-w     --witdh [N]         Set Width
-h     --height [N]        Set Height
-x     --startx [N]        Set x start cord
-y     --starty [N]        Set y start cord

--n+
    t    type [S]   Set noise
           | perlin
           | os2
           | os2s
           | cell
           | val
           | valc
    s    seed [N]          Set Noise Seed
    f    freq [F]          Set Noise Frequency, [F] must be between 0.0 and 1.0
    o    oct [N]           Set Noise Ocatave
    g    gain [F]          Set Noise Gain
    ft   { fmb|riged|pp|dprog|dind } def:none 
    c    { eu|eusq|hybrid|manhat }
    ct   { d|cell|d2|d2add|d2sub|d2div|d2mul }
    dw   { grid|os2|os2r } def: os2
    3d   { xz|xy }
    cj [N]                 Set Cellular Jitter Mod
    dwamp  [N]             Set Domain Wrap Amplifier

-r     --write [S]         Create an image file (.png)

--cmd "[CMD]"
--cmdfile [FILE]           Read Color and Char format from file
cmd:
  p #[fg] [bg]
  r [FN] [N] [N] #[basefg] #[basebg] #[stepfg] #[stepbg]
  l [FN] [N] #[basefg] #[basebg] #[stepfg] #[stepbg]
  c [STRING]
  b [N] #[new_color]
  f [N] #[new_color]

-p     --noprint           Toggle off terminal print
-d     --draw [S]          Set in Draw Mode/Method
           | char          Only Characters, Colorless
           | obg           Only Background Color
-e                         Start In Explorer Mode
```
