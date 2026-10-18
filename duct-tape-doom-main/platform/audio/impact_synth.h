/* Effects the WAD doesn't have, made up from nothing: sparks off hit tech,
 * lamps and screens dying, glass breaking. Each is rendered once as a mono
 * sample at a rate and held as unsigned 8-bit, like DOOM's own effects, so
 * it plays through the mixer as one of them. A seed picks the variation, so
 * no two hits sound alike. The sound starts at the moment of the hit. */
#ifndef DOOM_AUDIO_IMPACT_SYNTH_H
#define DOOM_AUDIO_IMPACT_SYNTH_H

enum {
    IMPACT_SPARK_CRACKLE,  /* random cracks thinning out */
    IMPACT_SPARK_FIZZ,     /* cracks over a sizzling hiss */
    IMPACT_SPARK_ZAP,      /* a short arc buzz, then cracks */
    IMPACT_LAMP_DIES,      /* the hum sputters, a pop, a few bits of tube */
    IMPACT_SCREEN_DIES,    /* a burst of static, the tube thumps off */
    IMPACT_GLASS_TINKLE,   /* shards landing, no break */
    IMPACT_GLASS_SHATTER,  /* the break alone, no shards falling */
    IMPACT_GLASS_BREAK,    /* the shatter, then shards landing */
    IMPACT_GLASS_BIG,      /* a longer shatter, a high pane's worth of shards */
    IMPACT_GLASS_FRACTURE, /* a branching crack front, no shards falling */
    IMPACT_GLASS_FRACTURE_BIG, /* a big pane's crack front, then its shards */
    IMPACT_STEP_STONE,     /* a footstep: heel and toe, a little grit */
    IMPACT_STEP_METAL,     /* with a faint ring of the plate */
    IMPACT_STEP_WOOD,      /* a hollow knock */
    IMPACT_STEP_SOFT,      /* muffled, on grass, dirt and carpet */
    IMPACT_SPLASH_WATER,   /* a step in water */
    IMPACT_SPLASH_THICK,   /* a step in nukage, slime, blood or lava */
    IMPACT_KINDS
};

/* Short name for files and settings. */
const char *Impact_Name(int kind);
/* Frames the kind needs at the rate. */
int Impact_Length(int kind, int rate);
/* Adds the kind into out (mono, about -1..1). */
void Impact_Render(int kind, unsigned seed, int rate, float *out, int frames);
/* The steady hum of a lamp, or a screen's, that a dying one cuts off. */
void Impact_Hum(int screen, unsigned seed, int rate, float *out, int frames);
/* As DOOM holds an effect: unsigned 8-bit, 128 the middle. */
void Impact_To8Bit(const float *in, unsigned char *out, int frames);

/* The pieces, in spark_synth.c, glass_synth.c, fracture_synth.c and step_synth.c. */
void Spark_Render(int kind, unsigned seed, int rate, float *out, int frames);
void Glass_Render(int kind, unsigned seed, int rate, float *out, int frames);
void Fracture_Render(int kind, unsigned seed, int rate, float *out, int frames);
void Step_Render(int kind, unsigned seed, int rate, float *out, int frames);
/* Count tiny bits still settling from start on, thinning out over a second. */
void Glass_Debris(float *out, int frames, int rate, unsigned seed, float start, int count, float amp);
/* Count shards let go over spread seconds from sill..sill+pane units above
   the floor, each ticking where it lands and bouncing on; tonal 0 is only
   the scratch of each landing, 1 mostly its ring. */
void Glass_Shards(float *out, int frames, int rate, unsigned seed, int count,
                  float sill, float pane, float spread, float amp, float tonal);
#endif
