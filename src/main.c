#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <z_/types/record.h>
#include <z_/types/base_util.h>
#include <z_/types/enum.h>
#include <z_/types/string.h>

#include <z_/proc/omp.h>

#include <z_/imp/ansi.h>
#include <z_/imp/argparse.h>
#include <z_/imp/sys.h>
#include <z_/imp/termio.h>
#include <z_/imp/time.h>

#include <zsf/map/ch.h>

#define FNL_IMPL
#include "fastnoise.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define HELP_INTRO\
    "Noise Explorer by zakarouf 2022 - 2023\n"\
    "Powered by nothings/stb & Auburn/FastNoiseLite\n"

#define HELP_TXT\
    "-w     --witdh [N]         Set Width\n"\
    "-h     --height [N]        Set Height\n"\
    "-x     --startx [N]        Set x start cord\n"\
    "-y     --starty [N]        Set y start cord\n"\
    "\n"\
    "-nt     --noise_type [S]   Set noise\n"\
    "           | perlin\n"\
    "           | os2\n"\
    "           | os2s\n"\
    "           | cell\n"\
    "           | val\n"\
    "           | valc\n"\
    "\n"\
    "-ns    --noise_seed [N]    Set Noise Seed\n"\
    "-nf    --noise_freq [F]    Set Noise Frequency,\n"\
    "                             [F] must be between 0.0 and 1.0\n"\
    "-no    --noise_oct [N]     Set Noise Ocatave\n"\
    "-ng    --noise_gain [F]    Set Noise Gain\n"\
    "\n"\
    "--cellj [N]                Set Cellular Jitter Mod\n"\
    "--domamp  [N]              Set Domain Wrap Amplifier\n"\
    "\n"\
    "-r     --write [S]         Write to a named[S] file\n"\
    "\n"\
    "-f     --file [S]              Read Color and Char format from file\n"\
    "-cc    --charlist [S]          Get Character maplist, lower to higher\n"\
    "--cp [r,g,b] [r,g,b]           Push Color, disables auto-gen colors\n"\
    "--clp [S] [N] [N] [r,g,b]x4    Push a generated color map\n"\
    "\n"\
    "-np    --noprint           Toggle oof terminal print\n"\
    "-d     --draw [S]          Set in Draw Mode/Method\n"\
    "           | char          Only Characters, Colorless\n"\
    "           | obg           Only Background Color\n"\
    "-e                         Start In Explorer Mode\n"


typedef z__RecordX((z__u32, ch), (z__u32, clr_fg, clr_bg)) MapPlot;
typedef zsf_MapCh(MapPlot) Map;

typedef z__Vector(z__u8, r, g, b) ColorRGB;
typedef z__Arr(ColorRGB) ColorRGBArr;
typedef z__u8Arr Color256Arr;

typedef struct Image Image;
struct Image {
    z__u8 *data;
    z__Vint2 size;
    z__i32 channel_count;
};

typedef struct OFormat {
    struct {
        ColorRGB *fg;
        ColorRGB *bg;
    } color;
    z__size color_len;
    z__size color_lenUsed;

    char *ch;
    z__size ch_len;
    z__size ch_lenUsed;
} OFormat;

typedef void (Drawfn)(Map *map, OFormat *oft);
typedef ColorRGB (ColorMathFn)(ColorRGB, ColorRGB);

void die(const char *msg)
{
    puts(msg);
    exit(1);
}

OFormat oft_new(const char *charl, z__size len)
{
    enum {ColorLen = 16, CharLen = 16};
    return (OFormat){
        .color = {
            .fg = z__MALLOC(ColorLen * sizeof(ColorRGB)),
            .bg = z__MALLOC(ColorLen * sizeof(ColorRGB)),
        },
        .color_len = ColorLen,
        .color_lenUsed = 0,

        .ch = len? strndup(charl, len)
                 : z__CALLOC(CharLen, sizeof(char)),
        .ch_len = len > 0? len: CharLen,
        .ch_lenUsed = len > 0? len: CharLen
    };
}

void oft_delete(OFormat *oft)
{
    z__FREE(oft->color.bg);
    z__FREE(oft->color.fg);
    z__FREE(oft->ch);

    oft->ch_len = 0;
    oft->ch_lenUsed = 0;

    oft->color_len = 0;
    oft->color_lenUsed = 0;
}

void oft_replace_chlist(OFormat *oft, const char *charl, z__size len)
{
    z__FREE(oft->ch);
    oft->ch = strndup(charl, len);
    oft->ch_len = len;
    oft->ch_lenUsed = len;
}

void oft_clr_expand(OFormat *oft, z__size by)
{
    oft->color_len += by;
    oft->color.bg = z__REALLOC(oft->color.bg, sizeof(*oft->color.bg) * oft->color_len);
    oft->color.fg = z__REALLOC(oft->color.fg, sizeof(*oft->color.fg) * oft->color_len);
}

void oft_push_color(OFormat *oft, ColorRGB fg, ColorRGB bg) {
    if(oft->color_len <= oft->color_lenUsed) {
        oft_clr_expand(oft, oft->color_len);
    }
    oft->color.bg[oft->color_lenUsed] = bg;
    oft->color.fg[oft->color_lenUsed] = fg;
    oft->color_lenUsed += 1;
}

void oft_push_color_map(
    OFormat *oft
  , z__size from
  , z__size upto
  , ColorRGB basefg
  , ColorRGB basebg
  , ColorRGB stepfg
  , ColorRGB stepbg
  , ColorMathFn step_callback)
{
    z__size fdmax = z__util_max_unsafe(from, upto);    
    z__size fdmin = z__util_min_unsafe(from, upto);

    if(fdmax > oft->color_len) {
        oft_clr_expand(oft, (fdmax - oft->color_len) + 1);
    }

    for(;fdmin < fdmax; fdmin++) {
        basebg = step_callback(basebg, stepbg);
        basefg = step_callback(basefg, stepfg);
        oft->color.bg[fdmin] = basebg;
        oft->color.fg[fdmin] = basefg;
    }
    
    if(fdmin > oft->color_lenUsed) oft->color_lenUsed = fdmin;
}

ColorRGB clr_add(ColorRGB x, ColorRGB y)
{   
    ColorRGB z;
    z__Vector3_A(x, y, +, &z);
    return z;
}

ColorRGB clr_sub(ColorRGB x, ColorRGB y)
{   
    ColorRGB z;
    z__Vector3_A(x, y, -, &z);
    return z;
}

ColorRGB clr_mul(ColorRGB x, ColorRGB y)
{   
    ColorRGB z;
    z__Vector3_A(x, y, *, &z);
    return z;
}

ColorRGB clr_div(ColorRGB x, ColorRGB y)
{   
    ColorRGB z;
    z__Vector3_A(x, y, *, &z);
    return z;
}

void oft_change_color(OFormat *oft, z__size idx, ColorRGB fg, ColorRGB bg) {
    if(idx >= oft->color_lenUsed) return;

    oft->color.bg[idx] = bg;
    oft->color.fg[idx] = fg;
}

#if 0
Image Image_newLoad(const char * path, int with_channel)
{
    Image img;
    img.data = stbi_load(path, &img.size.x, &img.size.y, &img.channel_count, with_channel);
    if(!img.data) die("Image Not Loaded");
    return img;
}
#endif

Image Image_new(z__Vint2 size, int with_channel)
{
    return (Image) {
        .size = size,
        .channel_count = with_channel,
        .data = z__CALLOC(size.x * size.y * with_channel, sizeof(z__u8))
    };
}

void Image_free(Image *img)
{
    free(img->data);
    memset(img, 0, sizeof(*img));
}

Image Image_clone_resize(Image *src, z__Vint2 newsize)
{
    Image op = Image_new(newsize, src->channel_count);
    stbir_resize_uint8(src->data, src->size.x, src->size.y, 0, op.data, op.size.x, op.size.y, 0, src->channel_count);
    return op;
}

z__size Image_get_size(Image *src)
{
    return sizeof(*src->data) * src->size.x * src->size.y * src->channel_count;
}

Image Image_clone(Image *src)
{
    z__size sz = sizeof(*src->data) * src->size.x * src->size.y * src->channel_count;
    Image img = {
        .size = src->size,
        .channel_count = src->channel_count,
        .data = z__MALLOC(sz)
    };

    memcpy(img.data, src->data, sz);
    return img;
}

void Image_write_png(char const * path, Image *img)
{
    stbi_write_png(path, img->size.x, img->size.y, img->channel_count, img->data, img->size.x * img->channel_count);
}

Image Image_newFrom_map(Map *map, OFormat *oft)
{
    Image img = Image_new((z__Vint2){.x = map->size.x, .y = map->size.y}, 3);

    z__u8 *i = img.data,
          *endi = i + (img.size.x * img.size.y * img.channel_count);

    MapPlot *p = &zsf_MapCh_getcr(map, 0, 0, 0, 0),
            *endp = p + (map->size.x * map->size.y);

    while (p < endp && i < endi) {
        *i = oft->color.bg[p->clr_bg].r;
        i += 1;
        *i = oft->color.bg[p->clr_bg].g;
        i += 1;
        *i = oft->color.bg[p->clr_bg].b;
        i += 1;

        p += 1;
    }

    return img;
}

void draw_map_bgcolor(Map *map, OFormat *oft)
{
    MapPlot *p = map->chunks[0];
    for (size_t i = 0; i < map->size.y; i++) {
        for (size_t j = 0; j < map->size.x; j++) {
            ColorRGB *c = &oft->color.bg[p->clr_bg];
            fprintf(
                stdout
                /*, z__ansi_fmt((cl256_bg, %d)) "%c", p->clr_bg,
                    charlist.data[p->ch >= charlist.lenUsed?
                                charlist.lenUsed-1:p->ch]);
                */
                  , z__ansi_fmt((clrgb_bg, %u, %u, %u)) "%c", c->r, c->g, c->b, ' ');
            p += 1;
        }
        fputc('\n', stdout);
    }
}

void draw_map_char(Map *map, OFormat *oft)
{
    MapPlot *p = map->chunks[0];
    for (size_t i = 0; i < map->size.y; i++) {
        for (size_t j = 0; j < map->size.x; j++) {
            fputc(oft->ch[p->ch], stdout);
            p += 1;
        }
        fputc('\n', stdout);
    }
}

void gen_map(Map *map, OFormat *oft, fnl_state *noise, int startx, int starty)
{
    z__size f = oft->color_lenUsed/2;
    z__size g = oft->ch_lenUsed/2;
    z__size x = 0, y = 0;
    z__omp(parallel for private(x, y))
        for (x = 0; x < map->size.x; x++) {
            for (y = 0; y < map->size.y; y++) {
                float n = fnlGetNoise2D(noise, startx + x, starty + y);
                MapPlot plot = {
                    .ch = fmod((n+1) * g,  oft->ch_lenUsed),
                    .clr_bg = fmod((n+1.0) * f, oft->color_lenUsed),
                };
                zsf_MapCh_setcr(map, x, y, 0, 0, plot);
            }
        }
}

#if 0
void gen_map_seg(Map *map, z__Vint2 end, OFormat *oft, fnl_state *noise, int startx, int starty)
{
    z__size f = oft->color_lenUsed;
    z__size g = oft->ch_lenUsed;
    z__size x = 0, y = 0;
    end.x = z__util_min_unsafe(end.x, map->size.x);
    end.y = z__util_min_unsafe(end.y, map->size.y);

    z__omp(parallel for private(x, y))
        for (x = 0; x < end.x; x++) {
            for (y = 0; y < end.y; y++) {
                float n = fnlGetNoise2D(noise, startx + x, starty + y);
                MapPlot plot = {
                    .ch = z__u32((n+1) * g/2) % g,
                    .clr_bg = fmod(((n+1.0) * f/2.0), f),
                };
                zsf_MapCh_setcr(map, x, y, 0, 0, plot);
            }
        }
}
#endif

void explorer(Map *map, OFormat *oft, fnl_state *noise, Drawfn draw, int startx, int starty)
{
    char key = 0, tmpkey;
    Map *mapbg = z__MALLOC(sizeof(*mapbg));
    zsf_MapCh_createEmpty(mapbg, map->size.x, map->size.y, map->size.z, map->chunkRadius);

    fputs(z__ansi_scr((jump), (clear)), stdout);
    while(key != 'q') {
        switch(key) {
            break; case 'w': starty--;
            break; case 's': starty++;
            break; case 'a': startx--;
            break; case 'd': startx++;

            break; case 'W': starty -= 4;
            break; case 'S': starty += 4;
            break; case 'A': startx -= 4;
            break; case 'D': startx += 4;

            break; case '1': draw = draw_map_char;
            break; case '2': draw = draw_map_bgcolor;
        }

        gen_map(map, oft, noise, startx, starty);

        fputs(z__ansi_scr((jump)), stdout);
        draw(map, oft);
        fputs(z__ansi_fmt((plain)), stdout);

        tmpkey = z__termio_getkey_nowait();
        key = tmpkey? tmpkey: key;
        z__time_msleep(40);
    }
}

fnl_noise_type get_fnlnoisetype(char *arg)
{
    char **s = &arg;
    z__argp_start(s, 0, 1) {
        z__argp_ifarg_custom("perlin")      return FNL_NOISE_PERLIN;
        z__argp_elifarg_custom("os2")       return FNL_NOISE_OPENSIMPLEX2;
        z__argp_elifarg_custom("os2s")      return FNL_NOISE_OPENSIMPLEX2S;
        z__argp_elifarg_custom("cell")      return FNL_NOISE_CELLULAR;
        z__argp_elifarg_custom("val")       return FNL_NOISE_VALUE;
        z__argp_elifarg_custom("valc")      return FNL_NOISE_VALUE_CUBIC;
    }

    printf("`%s` Not a Valid Noise Type, Defaulting to Perlin\n", *s);
    return FNL_NOISE_PERLIN;
}

Drawfn* get_drawmethod(char *arg)
{
    char **s = &arg;
    z__argp_start(s, 0, 1) {
        z__argp_ifarg_custom("char")    return draw_map_char;
        z__argp_elifarg_custom("obg")      return draw_map_bgcolor;
    }

    printf("`%s` Not a Valid Draw Method, Defaulting to Only BG Color\n", *s);
    return draw_map_bgcolor;
}

ColorMathFn *get_colorstepmethod(char *arg)
{
    char **s = &arg;
    z__argp_start(s, 0, 1) {
        z__argp_ifarg_custom("add")     return clr_add;
        z__argp_elifarg_custom("sub")   return clr_sub;
        z__argp_elifarg_custom("mul")   return clr_mul;
        z__argp_elifarg_custom("div")   return clr_div;
    }

    printf("`%s` Not a Color Step Method, Defaulting to Add\n", *s);
    return clr_add;

}

int main(int argc, char *argv[])
{
    struct {
        z__u32 witdh, height;
        z__i32 x, y;
        fnl_state noise;
        z__u32 color;
        char const *write_to_file_name;
        z__u32 startx, starty;
        Drawfn *draw;
        char write_to_file:1
           , read_color:1
           , no_print:1
           , explorer:1
           , custom_oft_colorl:1;
    } ne = { 
        .height = 10
      , .witdh = 10
      , .noise = fnlCreateState()
      , .color = 255
      , .write_to_file_name = "stdout.png"
      , .draw = draw_map_bgcolor
    };

    /* Color and Char Format */
    OFormat oft = oft_new(
            "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. "
          , sizeof "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. " -1);


    z__argp_start(argv, 1, argc) {
        /**
         * Basic Stuff
         */
        z__argp_ifarg(&ne.witdh, "-w", "--width")
        z__argp_elifarg(&ne.height, "-h", "--height")
        
        z__argp_elifarg(&ne.x, "-x", "--startx")
        z__argp_elifarg(&ne.y, "-y", "--starty")

        /**
         * Noise Stuff
         */
        z__argp_elifarg_custom("-nt", "--noise_type") {
            z__argp_next();
            ne.noise.noise_type = get_fnlnoisetype(z__argp_get());
        }
        z__argp_elifarg(&ne.noise.seed, "-ns", "--noise_seed")
        z__argp_elifarg(&ne.noise.frequency, "-nf", "--noise_freq")
        z__argp_elifarg(&ne.noise.gain, "-ng", "--noise_gain")
        z__argp_elifarg(&ne.noise.octaves, "-no", "--noise_oct")

        z__argp_elifarg(&ne.noise.cellular_jitter_mod, "--cellj")
        z__argp_elifarg(&ne.noise.domain_warp_amp, "--domamp")

        /**
         * Write to a png file
         */
        z__argp_elifarg_custom("-r", "--write") {
            z__argp_next();
            ne.write_to_file = 1;
            ne.write_to_file_name = z__argp_get();
        }

        /**
         * Explorer Mode
         */
        z__argp_elifarg_custom("-e") {
            ne.explorer = 1;
        }

        /**
         * Draw Stuff
         */
        z__argp_elifarg_custom("-np", "--noprint") {
           ne.no_print = 1; 
        }

        z__argp_elifarg_custom("-d", "--draw") {
            z__argp_next();
            ne.draw = get_drawmethod(z__argp_get());
        }

        /**
         * OFormat Stuff
         */
        z__argp_elifarg_custom("--cc") {
            z__argp_next();
            oft_replace_chlist(&oft, z__argp_get(), strlen(z__argp_get()));
        }

        z__argp_elifarg(&ne.color, "-c", "--color_num")

        z__argp_elifarg_custom("-cp") {
            z__argp_next();
            ColorRGB cl[2] = {0};
            sscanf(z__argp_get(), "%hhu,%hhu,%hhu", &cl[0].r, &cl[0].g, &cl[0].b); z__argp_next();
            sscanf(z__argp_get(), "%hhu,%hhu,%hhu", &cl[1].r, &cl[1].g, &cl[1].b);
            oft_push_color(&oft, cl[0], cl[1]);
            ne.custom_oft_colorl = 1;
        }

        z__argp_elifarg_custom("-cp") {
            z__argp_next();
            ColorRGB cl[2] = {0};
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &cl[0].r, &cl[0].g, &cl[0].b); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &cl[1].r, &cl[1].g, &cl[1].b);
            oft_push_color(&oft, cl[0], cl[1]);
            ne.custom_oft_colorl = 1;
        }

        z__argp_elifarg_custom("--clprx") {
            z__argp_next();
            ColorMathFn *clrfn = get_colorstepmethod(z__argp_get()); z__argp_next();
            ColorRGB clr[4] = {0};
            z__u32 f = 0, t = 0;

            sscanf(z__argp_get(), "%u", &f); z__argp_next();
            sscanf(z__argp_get(), "%u", &t); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[0].r, &clr[0].g, &clr[0].b); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[1].r, &clr[1].g, &clr[1].b); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[2].r, &clr[2].g, &clr[2].b); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[3].r, &clr[3].g, &clr[3].b);

            oft_push_color_map(
                &oft, f, t, clr[0], clr[1], clr[2], clr[3], clrfn);
            ne.custom_oft_colorl = 1;
        }

        z__argp_elifarg_custom("--clpx") {
            z__argp_next();
            ColorMathFn *clrfn = get_colorstepmethod(z__argp_get()); z__argp_next();
            ColorRGB clr[4] = {0};
            z__u32 t = 0;

            sscanf(z__argp_get(), "%u", &t); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[0].r, &clr[0].g, &clr[0].b); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[1].r, &clr[1].g, &clr[1].b); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[2].r, &clr[2].g, &clr[2].b); z__argp_next();
            sscanf(z__argp_get(), "%02hhx%02hhx%02hhx", &clr[3].r, &clr[3].g, &clr[3].b);

            oft_push_color_map(
                &oft, oft.color_lenUsed, t, clr[0], clr[1], clr[2], clr[3], clrfn);
            ne.custom_oft_colorl = 1;
        }


        z__argp_elifarg_custom("-cf", "--colorfile") {
            ne.read_color = 1;
            //ne.bg_colorlist = fetch_color(z__argp_get());
        }

        /**
         * Help
         */
        z__argp_elifarg_custom("-?", "--help") {
            puts(HELP_INTRO HELP_TXT);
            return 0;
        }
    }


    /**
     * Auto Generate Color Map if not set by the user.
     */
    if(!ne.custom_oft_colorl) {
        oft_push_color_map(
            &oft, 0, ne.color
            , (ColorRGB){{0, 0, 0}}
            , (ColorRGB){{0, 0, 0}}
            , (ColorRGB){{1, 1, 1}}
            , (ColorRGB){{1, 1, 1}}
            , clr_add);
    }


    /**
     * Map to store noise data
     */
    Map *map = z__MALLOC(sizeof *map);
    zsf_MapCh_createEmpty(map, ne.witdh, ne.height, 1, 0);
   
    gen_map(map, &oft, &ne.noise, ne.x, ne.y);
    
    if(!ne.explorer) {
        if(!ne.no_print) {
            ne.draw(map, &oft);
        }
    } else {
        z__u32 x, y;
        z__termio_get_term_size(&x, &y);
        if(x > map->size.x || y > map->size.y) {
            explorer(map, &oft, &ne.noise, ne.draw, ne.x, ne.y);
        } else {
            printf("Your Terminal Size %d rows, %d colums are too small for generated noise map: %d x %d"
                    , y, x, map->size.x, map->size.y);
        }
    }
        
    if(ne.write_to_file) {
        Image img = Image_newFrom_map(map, &oft);
        Image_write_png(ne.write_to_file_name, &img);
        Image_free(&img);
    }

    oft_delete(&oft);
    zsf_MapCh_delete(map);
    z__FREE(map);
    return 0;
}
