#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
#include "ext/fastnoise.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ext/stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "ext/stb_image_resize.h"

#define HELP_INTRO\
    "Noise Explorer by zakarouf 2022 - 2023\n"\
    "Powered by nothings/stb & Auburn/FastNoiseLite\n"

#define HELP_TXT\
    "\nCOMMANDS:\n"\
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
    "-nf    --noise_freq [F]    Set Noise Frequency, [F] must be between 0.0 and 1.0\n"\
    "-no    --noise_oct [N]     Set Noise Ocatave\n"\
    "-ng    --noise_gain [F]    Set Noise Gain\n"\
    "\n"\
    "--cellj [N]                Set Cellular Jitter Mod\n"\
    "--domamp  [N]              Set Domain Wrap Amplifier\n"\
    "\n"\
    "-r     --write [S]         Create an image file (.png)\n"\
    "\n"\
    "--cmd \"[CMD]\"\n"\
    "--cmdfile [FILE]           Read Color and Char format from file\n"\
    "cmd:\n"\
    "  p #[fg] [bg]\n"\
    "  r [FN] [N] [N] #[basefg] #[basebg] #[stepfg] #[stepbg]\n"\
    "  l [FN] [N] #[basefg] #[basebg] #[stepfg] #[stepbg]\n"\
    "  c [STRING]\n"\
    "\n"\
    "-p     --noprint           Toggle off terminal print\n"\
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

struct ne_state {
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
       , custom_oft_colorl:1
       , verbose:1
       , exit:1;
};

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

void oft_change_color(OFormat *oft, z__size idx, ColorRGB fg, ColorRGB bg) {
    if(idx >= oft->color_lenUsed) return;

    oft->color.bg[idx] = bg;
    oft->color.fg[idx] = fg;
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

int oft_push_color_map_strparse(char const *str, OFormat *oft, int use_range)
{
    int ret = 1;
    z__size from = oft->color_lenUsed, to = oft->color_lenUsed;
    ColorMathFn *colorstepmethod;
    ColorRGB cl[4] = {0};

    if(strncmp(str, "add", 3) == 0) {
        colorstepmethod = clr_add;
    } else if(strncmp(str, "sub", 3) == 0) {
        colorstepmethod = clr_sub;
    } else if(strncmp(str, "mul", 3) == 0) {
        colorstepmethod = clr_mul;
    } else if(strncmp(str, "div", 3) == 0) {
        colorstepmethod = clr_div;
    } else {
        colorstepmethod = clr_add;
        ret = 0;
    }

    if(use_range) {
        sscanf(str+4, "%zu %zu "
                      "%02hhx%02hhx%02hhx "
                      "%02hhx%02hhx%02hhx "
                      "%02hhx%02hhx%02hhx "
                      "%02hhx%02hhx%02hhx"
                    , &from, &to
                    , &cl[0].r, &cl[0].g, &cl[0].b
                    , &cl[1].r, &cl[1].g, &cl[1].b
                    , &cl[2].r, &cl[2].g, &cl[2].b
                    , &cl[3].r, &cl[3].g, &cl[3].b);
    } else {
        sscanf(str+4, "%zu "
                      "%02hhx%02hhx%02hhx "
                      "%02hhx%02hhx%02hhx "
                      "%02hhx%02hhx%02hhx "
                      "%02hhx%02hhx%02hhx"
                    , &to
                    , &cl[0].r, &cl[0].g, &cl[0].b
                    , &cl[1].r, &cl[1].g, &cl[1].b
                    , &cl[2].r, &cl[2].g, &cl[2].b
                    , &cl[3].r, &cl[3].g, &cl[3].b);

    }

    oft_push_color_map(oft, from, to, cl[0], cl[1], cl[2], cl[3], colorstepmethod);

    return ret;
}

int oft_push_color_strparse(char const *str, OFormat *oft)
{
    ColorRGB cl[2];
    int ret = sscanf(str, "%02hhx%02hhx%02hhx "
                          "%02hhx%02hhx%02hhx"
                        , &cl[0].r, &cl[0].g, &cl[0].b
                        , &cl[1].r, &cl[1].g, &cl[1].b);

    oft_push_color(oft, cl[0], cl[1]);
    return ret;
}

typedef union oft_command_parse_result {
    z__RecordX(
        (z__u8,(1
                , color_changed
                , ch_changed))
    ) st;
    z__u32 raw;
} oft_command_parse_result;

oft_command_parse_result oft_command_parse(char const *str, OFormat *oft)
{
    oft_command_parse_result ret = {0};
    switch(str[0]) {
        break; case 'p': oft_push_color_strparse(str+2, oft); ret.st.color_changed = 1;
        break; case 'r': oft_push_color_map_strparse(str+2, oft, true); ret.st.color_changed = 1;
        break; case 'l': oft_push_color_map_strparse(str+2, oft, false); ret.st.color_changed = 1;
        break; case 'c': {
            const char *tmp = str + 2;
            #define check(s) (s != '\n' && s != '\t' && isprint(s))
            while(check(*tmp)) tmp += 1;
            oft_replace_chlist(oft, str + 2, tmp - str + 2);
            ret.st.ch_changed = 1;
        }
    }
    return ret;
}

oft_command_parse_result oft_readFromFile(OFormat *oft, char const *filepath)
{
    oft_command_parse_result res = {0};
    FILE *fp = fopen(filepath, "r");
    if(fp == NULL) return res;
    
    z__String tmp = z__String_new(1024);
    while(fgets(tmp.data, tmp.len, fp) != NULL) {
        tmp.data[tmp.len-1] = 0;
        res.raw |= oft_command_parse(tmp.data, oft).raw;
    }

    fclose(fp);
    return res;
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

void explorer(Map *map, OFormat *oft, fnl_state *noise, Drawfn draw, z__u32 x, z__u32 y)
{
    char key = 0, tmpkey;
    Map *mapbg = z__MALLOC(sizeof(*mapbg));
    zsf_MapCh_createEmpty(mapbg, map->size.x, map->size.y, map->size.z, map->chunkRadius);

    fputs(z__ansi_scr((cur_hide), (jump), (clear)), stdout);
    z__termio_echo(false);
    while(key != 'q') {
        switch(key) {
            break; case 'w': y--;
            break; case 's': y++;
            break; case 'a': x--;
            break; case 'd': x++;

            break; case 'W': y -= 4;
            break; case 'S': y += 4;
            break; case 'A': x -= 4;
            break; case 'D': x += 4;

            break; case '1': draw = draw_map_char;
            break; case '2': draw = draw_map_bgcolor;
        }

        gen_map(map, oft, noise, x, y);

        fputs(z__ansi_scr((jump)), stdout);
        draw(map, oft);
        fputs(z__ansi_fmt((plain)), stdout);

        tmpkey = z__termio_getkey_nowait();
        key = tmpkey? tmpkey: key;
        z__time_msleep(40);
    }
    fputs(z__ansi_scr((cur_show)), stdout);
    z__termio_echo(true);

    fprintf(stdout, "x - %u\n"
                    "y - %u\n", x, y);
}

void print_state_details(struct ne_state *ne, OFormat *oft, Map *map)
{
    fputs( "\n"
         "NE Report\n"
         "==========", stdout);

    z__u32 tx, ty;
    z__termio_get_term_size(&tx, &ty);

    fprintf(stdout,
        "\n" "Term Size: %d x %d"
        "\n" "Map Size: %d x %d"
        "\n" "Oft charlist: %zu"
        "\n" "Oft colorlist: %zu"
        "\n"
        , tx, ty
        , map->size.x, map->size.y
        , oft->ch_lenUsed
        , oft->color_lenUsed
    );

    fputs( "\n"
         "Oft Colors\n"
         "===========\n"
         "  fg      bg", stdout);

    for (size_t i = 0; i < oft->color_lenUsed; i++) {
        ColorRGB bg = oft->color.bg[i];
        ColorRGB fg = oft->color.fg[i];
        fprintf(stdout, "\n" 
            "%02hhx%02hhx%02hhx %02hhx%02hhx%02hhx"
            , fg.r, fg.g, fg.b, bg.r, bg.g, bg.b);
    }

    fputc('\n', stdout);

    fputs( "\n"
         "System\n"
         "=======", stdout);
    fprintf(stdout,
        "\n" "Ram Usage: %zu bytes"
        "\n" "Map Size: %zu bytes + Struct %zu bytes"
        "\n" "Oft Color: %zu bytes"
        "\n" "Oft Char: %zu bytes"
    , z__sys_getRamUsage()
    , sizeof(**map->chunks) * map->size.x * map->size.y * map->size.z * map->chunkAndObjCount, sizeof(*map)
    , sizeof(*oft->color.bg) * 2 * oft->color_len
    , sizeof(*oft->ch) * oft->ch_len);

    fputc('\n', stdout);
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

struct ne_state argparse(char **argv, z__u32 argc, OFormat *oft)
{
    struct ne_state ne = { 
        .height = 15
      , .witdh = 40
      , .noise = fnlCreateState()
      , .color = 255
      , .write_to_file_name = "stdout.png"
      , .draw = draw_map_bgcolor
    };

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
        z__argp_elifarg_custom("-p", "--noprint") {
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
            oft_replace_chlist(oft, z__argp_get(), strlen(z__argp_get()));
        }

        z__argp_elifarg(&ne.color, "-c", "--color_num")

        z__argp_elifarg_custom("--cmd") {
            z__argp_next();
            oft_command_parse(z__argp_get(), oft);
            ne.custom_oft_colorl = 1;
        }

        z__argp_elifarg_custom("--cmdfile") {
            z__argp_next();
            ne.custom_oft_colorl |= oft_readFromFile(oft, z__argp_get()).st.color_changed;
        }

        z__argp_elifarg_custom("-v", "--verbose") {
            ne.verbose = 1;
        }

        /**
         * Help
         */
        z__argp_elifarg_custom("-?", "--help") {
            puts(HELP_INTRO HELP_TXT);
            ne.exit = 1;
            return ne;
        }
    }

    return ne;
}

int main(int argc, char *argv[])
{

    /* Color and Char Format */
    OFormat oft = oft_new("0123456789ABCDEF", sizeof "0123456789ABCDEF"-1);
            /*oft_new(
            "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. "
          , sizeof "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. " -1);
          */

    
    struct ne_state ne = argparse(argv, argc, &oft);
    if(ne.exit) {
        oft_delete(&oft);
        return 0;
    }

    /**
     * Auto Generate Color Map if not set by the user.
     */
    if(!ne.custom_oft_colorl) {
        oft_push_color_map(
            &oft, 0, ne.color
            , (ColorRGB){.raw = {0, 0, 0}}
            , (ColorRGB){.raw = {0, 0, 0}}
            , (ColorRGB){.raw = {1, 1, 1}}
            , (ColorRGB){.raw = {1, 1, 1}}
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
            fputs(z__ansi_fmt((plain)), stdout);
        }
    } else {
        z__u32 x, y;
        z__termio_get_term_size(&x, &y);
        if(x > map->size.x && y > map->size.y) {
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

    if(ne.verbose) {
        print_state_details(&ne, &oft, map);
    }

    oft_delete(&oft);
    zsf_MapCh_delete(map);
    z__FREE(map);
    return 0;
}
