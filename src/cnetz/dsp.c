/* C-Netz audio processing
 *
 * (C) 2016 by Andreas Eversberg <jolly@eversberg.eu>
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "../common/debug.h"
#include "../common/timer.h"
#include "cnetz.h"
#include "sysinfo.h"
#include "telegramm.h"
#include "dsp.h"

/* test function to mirror received audio from ratio back to radio */
//#define TEST_SCRABLE
/* test the audio quality after cascading two scramblers (TEST_SCRABLE must be defined) */
//#define TEST_UNSCRABLE

#define PI		M_PI

#define BITRATE		5280.0	/* bits per second */
#define BLOCK_BITS	198	/* duration of one time slot including pause at beginning and end */

#ifdef TEST_SCRABLE
jitter_t scrambler_test_jb;
scrambler_t scrambler_test_scrambler1;
scrambler_t scrambler_test_scrambler2;
#endif

static int16_t ramp_up[256], ramp_down[256];

void dsp_init(void)
{
}

static void dsp_init_ramp(cnetz_t *cnetz)
{
	double c;
        int i;
	int16_t deviation = cnetz->fsk_deviation;

	PDEBUG(DDSP, DEBUG_DEBUG, "Generating smooth ramp table.\n");
	for (i = 0; i < 256; i++) {
		c = cos((double)i / 256.0 * PI);
#if 0
		if (c < 0)
			c = -sqrt(-c);
		else
			c = sqrt(c);
#endif
		ramp_down[i] = (int)(c * (double)deviation);
		ramp_up[i] = -ramp_down[i];
	}
}

/* Init transceiver instance. */
int dsp_init_sender(cnetz_t *cnetz, int measure_speed, double clock_speed[2], double deviation, double noise)
{
	int rc = 0;
	double size;

	PDEBUG(DDSP, DEBUG_DEBUG, "Init FSK for 'Sender'.\n");

	if (measure_speed) {
		cnetz->measure_speed = measure_speed;
		cant_recover = 1;
	}

	if (clock_speed[0] > 1000 || clock_speed[0] < -1000 || clock_speed[1] > 1000 || clock_speed[1] < -1000) {
		PDEBUG(DDSP, DEBUG_ERROR, "Clock speed %.1f,%.1f ppm out of range! Plese use range between +-1000 ppm!\n", clock_speed[0], clock_speed[1]);
		return -EINVAL;
	}
	PDEBUG(DDSP, DEBUG_INFO, "Using clock speed of %.1f ppm (RX) and %.1f ppm (TX) to correct sound card's clock.\n", clock_speed[0], clock_speed[1]);

	cnetz->fsk_bitduration = (double)cnetz->sender.samplerate / ((double)BITRATE / (1.0 + clock_speed[1] / 1000000.0));
	cnetz->fsk_tx_bitstep = 1.0 / cnetz->fsk_bitduration;
	PDEBUG(DDSP, DEBUG_DEBUG, "Use %.4f samples for one bit duration @ %d.\n", cnetz->fsk_bitduration, cnetz->sender.samplerate);

	size = cnetz->fsk_bitduration * (double)BLOCK_BITS * 16.0; /* 16 blocks for distributed frames */
	cnetz->fsk_tx_buffer_size = size * 1.1; /* more to compensate clock speed */
	cnetz->fsk_tx_buffer = calloc(sizeof(int16_t), cnetz->fsk_tx_buffer_size);
	if (!cnetz->fsk_tx_buffer) {
		PDEBUG(DDSP, DEBUG_DEBUG, "No memory!\n");
		rc = -ENOMEM;
		goto error;
	}

	/* create devation and ramp */
	if (deviation > 1.0)
		deviation = 1.0;
	cnetz->fsk_deviation = (int16_t)(deviation * 32766.9); /* be sure not to overflow -32767 .. 32767 */
	dsp_init_ramp(cnetz);
	cnetz->fsk_noise = noise;

	/* create speech buffer */
	cnetz->dsp_speech_buffer = calloc(sizeof(int16_t), cnetz->sender.samplerate); /* buffer is greater than sr/1.1, just to be secure */
	if (!cnetz->dsp_speech_buffer) {
		PDEBUG(DDSP, DEBUG_DEBUG, "No memory!\n");
		rc = -ENOMEM;
		goto error;
	}

	/* reinit the sample rate to shrink/expand audio */
	init_samplerate(&cnetz->sender.srstate, (double)cnetz->sender.samplerate / 1.1); /* 66 <-> 60 */

	rc = fsk_fm_init(&cnetz->fsk_demod, cnetz, cnetz->sender.samplerate, (double)BITRATE / (1.0 + clock_speed[0] / 1000000.0));
	if (rc < 0)
		goto error;

	/* init scrambler for shrinked audio */
	scrambler_setup(&cnetz->scrambler_tx, (double)cnetz->sender.samplerate / 1.1); 
	scrambler_setup(&cnetz->scrambler_rx, (double)cnetz->sender.samplerate / 1.1);

	/* reinit jitter buffer for 8000 kHz */
	jitter_destroy(&cnetz->sender.audio);
	rc = jitter_create(&cnetz->sender.audio, 8000 / 5);
	if (rc < 0)
		goto error;

	/* init compander, according to C-Netz specs, attack and recovery time
	 * shall not exceed according to ITU G.162 */
	init_compander(&cnetz->cstate, 8000, 5.0, 22.5, 32767);

#ifdef TEST_SCRABLE
	rc = jitter_create(&scrambler_test_jb, cnetz->sender.samplerate / 5);
	if (rc < 0) {
		PDEBUG(DDSP, DEBUG_ERROR, "Failed to init jitter buffer for scrambler test!\n");
		exit(0);
	}
	scrambler_setup(&scrambler_test_scrambler1, cnetz->sender.samplerate);
	scrambler_setup(&scrambler_test_scrambler2, cnetz->sender.samplerate);
#endif

	return 0;

error:
	dsp_cleanup_sender(cnetz);

	return rc;
}

void dsp_cleanup_sender(cnetz_t *cnetz)
{
        PDEBUG(DDSP, DEBUG_DEBUG, "Cleanup FSK for 'Sender'.\n");

	if (cnetz->fsk_tx_buffer)
		free(cnetz->fsk_tx_buffer);
	if (cnetz->dsp_speech_buffer)
		free(cnetz->dsp_speech_buffer);
}

/* receive sample time and calculate speed against system clock
 * tx: indicates transmit stream
 * result: if set the actual signal speed is used (instead of sample rate) */
void calc_clock_speed(cnetz_t *cnetz, uint64_t samples, int tx, int result)
{
	struct clock_speed *cs = &cnetz->clock_speed;
	double ti;
	double speed_ppm_rx[2], speed_ppm_tx[2];

	if (!cnetz->measure_speed)
		return;

	if (result)
		tx += 2;

	ti = get_time();

	/* skip some time to avoid false mesurement due to filling of buffers */
	if (cs->meas_ti == 0.0) {
		cs->meas_ti = ti + 1.0;
		return;
	}
	if (cs->meas_ti > ti)
		return;

	/* start sample counting */
	if (cs->start_ti[tx] == 0.0) {
		cs->start_ti[tx] = ti;
		cs->spl_count[tx] = 0;
		return;
	}

	/* add elapsed time */
	cs->last_ti[tx] = ti;
	cs->spl_count[tx] += samples;

	/* only calculate speed, if one second has elapsed */
	if (ti - cs->meas_ti <= 1.0)
		return;
	cs->meas_ti += 1.0;

	if (!cs->spl_count[2] || !cs->spl_count[3])
		return;
	speed_ppm_rx[0] = ((double)cs->spl_count[0] / (double)cnetz->sender.samplerate) / (cs->last_ti[0] - cs->start_ti[0]) * 1000000.0 - 1000000.0;
	speed_ppm_tx[0] = ((double)cs->spl_count[1] / (double)cnetz->sender.samplerate) / (cs->last_ti[1] - cs->start_ti[1]) * 1000000.0 - 1000000.0;
	speed_ppm_rx[1] = ((double)cs->spl_count[2] / (double)cnetz->sender.samplerate) / (cs->last_ti[2] - cs->start_ti[2]) * 1000000.0 - 1000000.0;
	speed_ppm_tx[1] = ((double)cs->spl_count[3] / (double)cnetz->sender.samplerate) / (cs->last_ti[3] - cs->start_ti[3]) * 1000000.0 - 1000000.0;
	PDEBUG(DDSP, DEBUG_NOTICE, "Clock: RX=%.2f TX=%.2f; Signal: TX=%.2f RX=%.2f ppm\n", speed_ppm_rx[0], speed_ppm_tx[0], speed_ppm_rx[1], speed_ppm_tx[1]);
}

static int fsk_nothing_encode(cnetz_t *cnetz)
{
	int16_t *spl;
	double phase, bitstep, r;
	int i, count;

	spl = cnetz->fsk_tx_buffer;
	phase = cnetz->fsk_tx_phase;
	bitstep = cnetz->fsk_tx_bitstep * 256.0;

	if (cnetz->fsk_noise) {
		r = cnetz->fsk_noise;
		/* add 198 bits of noise */
		for (i = 0; i < 198; i++) {
			do {
				*spl++ = (double)((int16_t)(random() & 0xffff)) * r;
				phase += bitstep;
			} while (phase < 256.0);
			phase -= 256.0;
		}
	} else {
		/* add 198 bits of silence */
		for (i = 0; i < 198; i++) {
			do {
				*spl++ = 0;
				phase += bitstep;
			} while (phase < 256.0);
			phase -= 256.0;
		}
	}

	/* depending on the number of samples, return the number */
	count = ((uintptr_t)spl - (uintptr_t)cnetz->fsk_tx_buffer) / sizeof(*spl);

	cnetz->fsk_tx_phase = phase;
	cnetz->fsk_tx_buffer_length = count;

	return count;
}

/* encode one data block into samples
 * input: 184 data bits (including barker code)
 * output: samples
 * return number of samples */
static int fsk_block_encode(cnetz_t *cnetz, const char *bits)
{
	/* alloc samples, add 1 in case there is a rest */
	int16_t *spl;
	double phase, bitstep, deviation;
	int i, count;
	char last;

	deviation = cnetz->fsk_deviation;
	spl = cnetz->fsk_tx_buffer;
	phase = cnetz->fsk_tx_phase;
	bitstep = cnetz->fsk_tx_bitstep * 256.0;

	/* add 7 bits of pause */
	for (i = 0; i < 7; i++) {
		do {
			*spl++ = 0;
			phase += bitstep;
		} while (phase < 256.0);
		phase -= 256.0;
	}
	/* add 184 bits */
	last = ' ';
	for (i = 0; i < 184; i++) {
		switch (last) {
		case ' ':
			if (bits[i] == '1') {
				/* ramp up from 0 */
				do {
					*spl++ = ramp_up[(int)phase] / 2 + deviation / 2;
					phase += bitstep;
				} while (phase < 256.0);
				phase -= 256.0;
			} else {
				/* ramp down from 0 */
				do {
					*spl++ = ramp_down[(int)phase] / 2 - deviation / 2;
					phase += bitstep;
				} while (phase < 256.0);
				phase -= 256.0;
			}
			break;
		case '1':
			if (bits[i] == '1') {
				/* stay up */
				do {
					*spl++ = deviation;
					phase += bitstep;
				} while (phase < 256.0);
				phase -= 256.0;
			} else {
				/* ramp down */
				do {
					*spl++ = ramp_down[(int)phase];
					phase += bitstep;
				} while (phase < 256.0);
				phase -= 256.0;
			}
			break;
		case '0':
			if (bits[i] == '1') {
				/* ramp up */
				do {
					*spl++ = ramp_up[(int)phase];
					phase += bitstep;
				} while (phase < 256.0);
				phase -= 256.0;
			} else {
				/* stay down */
				do {
					*spl++ = -deviation;
					phase += bitstep;
				} while (phase < 256.0);
				phase -= 256.0;
			}
			break;
		}
		last = bits[i];
	}
	/* add 7 bits of pause */
	if (last == '0') {
		/* ramp up to 0 */
		do {
			*spl++ = ramp_up[(int)phase] / 2 - deviation / 2;
			phase += bitstep;
		} while (phase < 256.0);
		phase -= 256.0;
	} else {
		/* ramp down to 0 */
		do {
			*spl++ = ramp_down[(int)phase] / 2 + deviation / 2;
			phase += bitstep;
		} while (phase < 256.0);
		phase -= 256.0;
	}
	for (i = 1; i < 7; i++) {
		do {
			*spl++ = 0;
			phase += bitstep;
		} while (phase < 256.0);
		phase -= 256.0;
	}

	/* depending on the number of samples, return the number */
	count = ((uintptr_t)spl - (uintptr_t)cnetz->fsk_tx_buffer) / sizeof(*spl);

	cnetz->fsk_tx_phase = phase;
	cnetz->fsk_tx_buffer_length = count;

	return count;
}

/* encode one distributed data block into samples
 * input: 184 data bits (including barker code)
 * output: samples
 * 	if a sample contains 0x8000, it indicates where to insert speech block
 * return number of samples */
static int fsk_distributed_encode(cnetz_t *cnetz, const char *bits)
{
	/* alloc samples, add 1 in case there is a rest */
	int16_t *spl, *marker;
	double phase, bitstep, deviation;
	int i, j, count;
	char last;

	deviation = cnetz->fsk_deviation;
	spl = cnetz->fsk_tx_buffer;
	phase = cnetz->fsk_tx_phase;
	bitstep = cnetz->fsk_tx_bitstep * 256.0;

	/* add 2 * (1+4+1 + 60) bits of pause / for speech */
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 6; j++) {
			do {
				*spl++ = 0;
				phase += bitstep;
			} while (phase < 256.0);
			phase -= 256.0;
		}
		marker = spl;
		for (j = 0; j < 60; j++) {
			do {
				*spl++ = 0;
				phase += bitstep;
			} while (phase < 256.0);
			phase -= 256.0;
		}
		*marker = -32768; /* indicator for inserting speech */
	}
	/* add 46 * (1+4+1 + 60) bits */
	for (i = 0; i < 46; i++) {
		/* unmodulated bit */
		do {
			*spl++ = 0;
			phase += bitstep;
		} while (phase < 256.0);
		phase -= 256.0;
		last = ' ';
		for (j = 0; j < 4; j++) {
			switch (last) {
			case ' ':
				if (bits[i * 4 + j] == '1') {
					/* ramp up from 0 */
					do {
						*spl++ = ramp_up[(int)phase] / 2 + deviation / 2;
						phase += bitstep;
					} while (phase < 256.0);
					phase -= 256.0;
				} else {
					/* ramp down from 0 */
					do {
						*spl++ = ramp_down[(int)phase] / 2 - deviation / 2;
						phase += bitstep;
					} while (phase < 256.0);
					phase -= 256.0;
				}
				break;
			case '1':
				if (bits[i * 4 + j] == '1') {
					/* stay up */
					do {
						*spl++ = deviation;
						phase += bitstep;
					} while (phase < 256.0);
					phase -= 256.0;
				} else {
					/* ramp down */
					do {
						*spl++ = ramp_down[(int)phase];
						phase += bitstep;
					} while (phase < 256.0);
					phase -= 256.0;
				}
				break;
			case '0':
				if (bits[i * 4 + j] == '1') {
					/* ramp up */
					do {
						*spl++ = ramp_up[(int)phase];
						phase += bitstep;
					} while (phase < 256.0);
					phase -= 256.0;
				} else {
					/* stay down */
					do {
						*spl++ = -deviation;
						phase += bitstep;
					} while (phase < 256.0);
					phase -= 256.0;
				}
				break;
			}
			last = bits[i * 4 + j];
		}
		/* unmodulated bit */
		if (last == '0') {
			/* ramp up to 0 */
			do {
				*spl++ = ramp_up[(int)phase] / 2 - deviation / 2;
				phase += bitstep;
			} while (phase < 256.0);
			phase -= 256.0;
		} else {
			/* ramp down to 0 */
			do {
				*spl++ = ramp_down[(int)phase] / 2 + deviation / 2;
				phase += bitstep;
			} while (phase < 256.0);
			phase -= 256.0;
		}
		marker = spl;
		for (j = 0; j < 60; j++) {
			do {
				*spl++ = 0;
				phase += bitstep;
			} while (phase < 256.0);
			phase -= 256.0;
		}
		*marker = -32768; /* indicator for inserting speech */
	}

	/* depending on the number of samples, return the number */
	count = ((uintptr_t)spl - (uintptr_t)cnetz->fsk_tx_buffer) / sizeof(*spl);

	cnetz->fsk_tx_phase = phase;
	cnetz->fsk_tx_buffer_length = count;

	return count;
}

void show_level(double level)
{
	char text[42] = "                                         ";

	if (level > 1.0)
		level = 1.0;
	if (level < -1.0)
		level = -1.0;
	text[20 - (int)(level * 20)] = '*';
	printf("%s\n", text);
}

/* decode samples and hut for bit changes
 * use deviation to find greatest slope of the signal (bit change)
 */
void sender_receive(sender_t *sender, int16_t *samples, int length)
{
	cnetz_t *cnetz = (cnetz_t *) sender;

	/* measure rx sample speed */
	calc_clock_speed(cnetz, length, 0, 0);

#ifdef TEST_SCRABLE
#ifdef TEST_UNSCRABLE
	scrambler(&scrambler_test_scrambler1, samples, length);
#endif
	jitter_save(&scrambler_test_jb, samples, length);
	return;
#endif

	fsk_fm_demod(&cnetz->fsk_demod, samples, length);
	return;
}

static int fsk_telegramm(cnetz_t *cnetz, int16_t *samples, int length)
{
	int count = 0, pos, copy, i, speech_length, speech_pos;
	int16_t *spl, *speech_buffer;
	const char *bits;

	speech_buffer = cnetz->dsp_speech_buffer;
	speech_length = cnetz->dsp_speech_length;
	speech_pos = cnetz->dsp_speech_pos;

again:
	/* there must be length, otherwise we would skip blocks */
	if (!length)
		return count;

	pos = cnetz->fsk_tx_buffer_pos;
	spl = cnetz->fsk_tx_buffer + pos;

	/* start new telegramm, so we generate one */
	if (pos == 0) {
		/* measure actual signal speed */
		if (cnetz->sched_ts == 0 && cnetz->sched_r_m == 0)
			calc_clock_speed(cnetz, cnetz->sender.samplerate * 24 / 10, 1, 1);

		/* switch to speech channel */
		if (cnetz->sched_switch_mode && cnetz->sched_r_m == 0) {
			if (--cnetz->sched_switch_mode == 0) {
				/* OgK / SpK(K) / SpK(V) */
				PDEBUG(DDSP, DEBUG_INFO, "Switching channel (mode)\n");
				cnetz->dsp_mode = cnetz->sched_dsp_mode;
			}
		}

		switch (cnetz->dsp_mode) {
		case DSP_MODE_OGK:
			if (((1 << cnetz->sched_ts) & si.ogk_timeslot_mask)) {
				if (cnetz->sched_r_m == 0) {
					/* set last time slot, so we can match received message from mobile station */
					cnetz->last_tx_timeslot = cnetz->sched_ts;
					PDEBUG(DDSP, DEBUG_DEBUG, "Transmitting 'Rufblock' at timeslot %d\n", cnetz->sched_ts);
					bits = cnetz_encode_telegramm(cnetz);
				} else {
					PDEBUG(DDSP, DEBUG_DEBUG, "Transmitting 'Meldeblock' at timeslot %d\n", cnetz->sched_ts);
					bits = cnetz_encode_telegramm(cnetz);
				}
				fsk_block_encode(cnetz, bits);
			} else {
				fsk_nothing_encode(cnetz);
			}
			break;
		case DSP_MODE_SPK_K:
			PDEBUG(DDSP, DEBUG_DEBUG, "Transmitting 'Konzentrierte Signalisierung'\n");
			bits = cnetz_encode_telegramm(cnetz);
			fsk_block_encode(cnetz, bits);
			break;
		case DSP_MODE_SPK_V:
			PDEBUG(DDSP, DEBUG_DEBUG, "Transmitting 'Verteilte Signalisierung'\n");
			bits = cnetz_encode_telegramm(cnetz);
			fsk_distributed_encode(cnetz, bits);
			break;
		default:
			fsk_nothing_encode(cnetz);
		}

		if (cnetz->dsp_mode == DSP_MODE_SPK_V) {
			/* count sub frame */
			cnetz->sched_ts += 8;
		} else {
			/* count slot */
			if (cnetz->sched_r_m == 0)
				cnetz->sched_r_m = 1;
			else {
				cnetz->sched_r_m = 0;
				cnetz->sched_ts++;
			}
		}
		if (cnetz->sched_ts == 32)
			cnetz->sched_ts = 0;
	}

	copy = cnetz->fsk_tx_buffer_length - pos;
	if (length < copy)
		copy = length;
	for (i = 0; i < copy; i++) {
		if (*spl == -32768) {
			/* marker found to insert new chunk of audio */
			jitter_load(&cnetz->sender.audio, speech_buffer, 100);
			compress_audio(&cnetz->cstate, speech_buffer, 100);
			speech_length = samplerate_upsample(&cnetz->sender.srstate, speech_buffer, 100, speech_buffer);
			if (cnetz->scrambler)
				scrambler(&cnetz->scrambler_tx, speech_buffer, speech_length);
			/* pre-emphasis is done by cnetz code, not by common code */
			/* pre-emphasis makes bad sound in conjunction with scrambler, so we disable */
			if (cnetz->pre_emphasis && !cnetz->scrambler)
				pre_emphasis(&cnetz->estate, speech_buffer, speech_length);
			speech_pos = 0;
		}
		/* copy speech as long as we have something left in buffer */
		if (speech_pos < speech_length)
			*samples++ = speech_buffer[speech_pos++];
		else
			*samples++ = *spl;
		spl++;
	}
	cnetz->dsp_speech_length = speech_length;
	cnetz->dsp_speech_pos = speech_pos;
	pos += copy;
	count += copy;
	length -= copy;
	if (pos == cnetz->fsk_tx_buffer_length) {
		cnetz->fsk_tx_buffer_pos = 0;
		goto again;
	}

	cnetz->fsk_tx_buffer_pos = pos;

	return count;
}

/* Provide stream of audio toward radio unit */
void sender_send(sender_t *sender, int16_t *samples, int length)
{
	cnetz_t *cnetz = (cnetz_t *) sender;
	int count;

	/* measure tx sample speed */
	calc_clock_speed(cnetz, length, 1, 0);

#ifdef TEST_SCRABLE
	jitter_load(&scrambler_test_jb, samples, length);
	scrambler(&scrambler_test_scrambler2, samples, length);
	return;
#endif

	count = fsk_telegramm(cnetz, samples, length);
	if (count < length) {
		printf("length=%d < count=%d\n", length, count);
		printf("this shall not happen, so please fix!\n");
		exit(0);
	}
}
