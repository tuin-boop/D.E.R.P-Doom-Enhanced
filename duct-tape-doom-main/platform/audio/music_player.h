/* Plays a loaded score through one of the music synths (OPL or SoundFont). */
#ifndef DOOM_AUDIO_MUSIC_PLAYER_H
#define DOOM_AUDIO_MUSIC_PLAYER_H
#include "music_score.h"

typedef struct
{
    void	(*reset) (void);		/* silence, channels to defaults */
    void	(*event) (const score_event_t* event);
    void	(*mix) (float* stereo, int frames, float gain);	/* adds to the buffer */
} music_synth_t;

typedef struct
{
    const score_t*		score;
    const music_synth_t*	synth;
    int				next;	/* next event to play */
    uint32_t			frame;
    boolean			playing, looping;
} music_player_t;

void Player_Start (music_player_t* player, const score_t* score, const music_synth_t* synth, boolean loop);
void Player_Stop (music_player_t* player);
/* Adds the next frames of music to an interleaved stereo buffer. */
void Player_Mix (music_player_t* player, float* stereo, int frames, float gain);
#endif
