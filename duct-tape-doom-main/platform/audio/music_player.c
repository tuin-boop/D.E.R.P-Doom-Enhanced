/* Score playback: events go to the synth on their frame, and the synth
   renders the stretches between them. At the end a looping song starts
   over from silence; otherwise playback stops. */
#include "music_player.h"

void Player_Start (music_player_t* player, const score_t* score, const music_synth_t* synth, boolean loop)
{
    synth->reset ();
    *player = (music_player_t){ score, synth, 0, 0, true, loop };
}

void Player_Stop (music_player_t* player)
{
    if (player->synth)
	player->synth->reset ();
    player->playing = false;
}

void Player_Mix (music_player_t* player, float* stereo, int frames, float gain)
{
    const score_t* score = player->score;

    while (player->playing && frames > 0)
    {
	uint32_t run;

	while (player->next < score->count && score->events[player->next].frame <= player->frame)
	    player->synth->event (&score->events[player->next++]);
	if (player->next < score->count)
	    run = score->events[player->next].frame - player->frame;
	else if (player->frame < score->length)
	    run = score->length - player->frame;
	else
	{
	    player->synth->reset ();
	    player->next = 0;
	    player->frame = 0;
	    player->playing = player->looping && score->length > 0;
	    continue;
	}
	if (run > (uint32_t)frames)
	    run = frames;
	player->synth->mix (stereo, (int)run, gain);
	stereo += 2 * run;
	frames -= run;
	player->frame += run;
    }
}
