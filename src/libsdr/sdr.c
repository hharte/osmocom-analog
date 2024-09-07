/* SDR processing
 *
 * (C) 2017 by Andreas Eversberg <jolly@eversberg.eu>
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

enum paging_signal;

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#define __USE_GNU
#include <pthread.h>
#include <unistd.h>
#include "../libsample/sample.h"
#include "../libfm/fm.h"
#include "../libam/am.h"
#include <osmocom/core/timer.h>
#include "../libmobile/sender.h"
#include "sdr_config.h"
#include "sdr.h"
#ifdef HAVE_UHD
#include "uhd.h"
#endif
#ifdef HAVE_SOAPY
#include "soapy.h"
#endif
#include "../liblogging/logging.h"

/* enable to debug buffer handling */
//#define DEBUG_BUFFER

/* enable to test without oversampling filter */
//#define DISABLE_FILTER

/* usable bandwidth of IQ rate, because no filter is perfect */
#define USABLE_BANDWIDTH	0.75

/* limit the IQ level to prevent IIR filter from exceeding range of -1 .. 1 */
#define LIMIT_IQ_LEVEL		0.95

int sdr_rx_overflow = 0;

typedef struct sdr_thread {
	int use;
	volatile int running, exit;	/* flags to control exit of threads */
	int buffer_size;
	volatile float *buffer;
	float *buffer2;
	volatile int in, out;		/* in and out pointers (atomic, so no locking required) */
	int max_fill;			/* measure maximum buffer fill */
	double max_fill_timer;		/* timer to display/reset maximum fill */
	iir_filter_t lp[2];		/* filter for upsample/downsample IQ data */
} sdr_thread_t;

typedef struct sdr_chan {
	double		tx_frequency;	/* frequency used */
	double		rx_frequency;	/* frequency used */
	int		am;		/* use AM instead of FM */
	fm_mod_t	fm_mod;		/* modulator instance */
	fm_demod_t	fm_demod;	/* demodulator instance */
	am_mod_t	am_mod;		/* modulator instance */
	am_demod_t	am_demod;	/* demodulator instance */
	dispmeasparam_t	*dmp_rf_level;
	dispmeasparam_t	*dmp_freq_offset;
	dispmeasparam_t	*dmp_deviation;
} sdr_chan_t;

typedef struct sdr {
	int		threads;	/* use threads */
	int		oversample;	/* oversample IQ rate */
	sdr_thread_t	thread_read,
			thread_write;
	sdr_chan_t	*chan;		/* settings for all channels */
	int		paging_channel;	/* if set, points to paging channel */
	sdr_chan_t	paging_chan;	/* settings for extra paging channel */
	int		channels;	/* number of frequencies */
	double		amplitude;	/* amplitude of each carrier */
	int		samplerate;	/* sample rate of audio data */
	int		buffer_size;	/* buffer in audio samples */
	double		interval;	/* how often to process the loop */
	wave_rec_t	wave_rx_rec;
	wave_rec_t	wave_tx_rec;
	wave_play_t	wave_rx_play;
	wave_play_t	wave_tx_play;
	float		*modbuff;	/* buffer for transmodulation */
	sample_t	*modbuff_I;
	sample_t	*modbuff_Q;
	sample_t	*modbuff_carrier;
	sample_t	*wavespl0;	/* sample buffer for wave generation */
	sample_t	*wavespl1;
} sdr_t;

static void show_spectrum(const char *direction, double halfbandwidth, double center, double *frequency, double paging_frequency, int num)
{
	char text[80];
	int i, x;

	memset(text, ' ', 79);
	text[79] = '\0';

	// FIXME: better solution
	if (num > 9)
		num = 9;

	for (i = 0; i < num; i++) {
		x = (frequency[i] - center) / halfbandwidth * 39.0 + 39.5;
		if (x >= 0 && x < 79)
			text[x] = '1' + i;
	}
	if (paging_frequency) {
		x = (paging_frequency - center) / halfbandwidth * 39.0 + 39.5;
		if (x >= 0 && x < 79)
			text[x] = 'P';
	}

	LOGP(DSDR, LOGL_INFO, "%s Spectrum:\n%s\n---------------------------------------+---------------------------------------\n", direction, text);
	for (i = 0; i < num; i++)
		LOGP(DSDR, LOGL_INFO, "Frequency %c = %.4f MHz\n", '1' + i, frequency[i] / 1e6);
	if (paging_frequency)
		LOGP(DSDR, LOGL_INFO, "Frequency P = %.4f MHz (Paging Frequency)\n", paging_frequency / 1e6);
}

void *sdr_open(int __attribute__((__unused__)) direction, const char __attribute__((__unused__)) *device, double *tx_frequency, double *rx_frequency, int *am, int channels, double paging_frequency, int samplerate, int buffer_size, double interval, double max_deviation, double max_modulation, double modulation_index)
{
	sdr_t *sdr;
	int threads = 1, oversample = 1; /* always use threads */
	double bandwidth;
	double tx_center_frequency = 0.0, rx_center_frequency = 0.0;
	int rc;
	int c;

	LOGP(DSDR, LOGL_DEBUG, "Open SDR device\n");

	if (sdr_config->samplerate != samplerate) {
		if (samplerate > sdr_config->samplerate) {
			LOGP(DSDR, LOGL_ERROR, "SDR sample rate must be greater than audio sample rate!\n");
			LOGP(DSDR, LOGL_ERROR, "You selected an SDR rate of %d and an audio rate of %d.\n", sdr_config->samplerate, samplerate);
			return NULL;
		}
		if ((sdr_config->samplerate % samplerate)) {
			LOGP(DSDR, LOGL_ERROR, "SDR sample rate must be a multiple of audio sample rate!\n");
			LOGP(DSDR, LOGL_ERROR, "You selected an SDR rate of %d and an audio rate of %d.\n", sdr_config->samplerate, samplerate);
			return NULL;
		}
		oversample = sdr_config->samplerate / samplerate;
		threads = 1;
	}

	bandwidth = 2.0 * (max_deviation + max_modulation);
	if (bandwidth)
		LOGP(DSDR, LOGL_INFO, "Require bandwidth of each channel is 2 * (%.1f deviation + %.1f modulation) = %.1f KHz\n", max_deviation / 1e3, max_modulation / 1e3, bandwidth / 1e3);

	sdr = calloc(sizeof(*sdr), 1);
	if (!sdr) {
		LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
		goto error;
	}
	sdr->channels = channels;
	sdr->amplitude = 1.0 / (double)channels;
	sdr->samplerate = samplerate;
	sdr->buffer_size = buffer_size;
	sdr->interval = interval;
	sdr->threads = threads; /* always required, because write may block */
	sdr->oversample = oversample;

	if (threads) {
		memset(&sdr->thread_read, 0, sizeof(sdr->thread_read));
		sdr->thread_read.buffer_size = sdr->buffer_size * 2 * sdr->oversample + 2;
		sdr->thread_read.buffer = calloc(sdr->thread_read.buffer_size, sizeof(*sdr->thread_read.buffer));
		if (!sdr->thread_read.buffer) {
			LOGP(DSDR, LOGL_ERROR, "No mem!\n");
			goto error;
		}
		sdr->thread_read.buffer2 = calloc(sdr->thread_read.buffer_size, sizeof(*sdr->thread_read.buffer2));
		if (!sdr->thread_read.buffer2) {
			LOGP(DSDR, LOGL_ERROR, "No mem!\n");
			goto error;
		}
		sdr->thread_read.in = sdr->thread_read.out = 0;
		if (oversample > 1) {
			iir_lowpass_init(&sdr->thread_read.lp[0], samplerate / 2.0, sdr_config->samplerate, 2);
			iir_lowpass_init(&sdr->thread_read.lp[1], samplerate / 2.0, sdr_config->samplerate, 2);
		}
		memset(&sdr->thread_write, 0, sizeof(sdr->thread_write));
		sdr->thread_write.buffer_size = sdr->buffer_size * 2 + 2;
		sdr->thread_write.buffer = calloc(sdr->thread_write.buffer_size, sizeof(*sdr->thread_write.buffer));
		if (!sdr->thread_write.buffer) {
			LOGP(DSDR, LOGL_ERROR, "No mem!\n");
			goto error;
		}
		sdr->thread_write.buffer2 = calloc(sdr->thread_write.buffer_size * sdr->oversample, sizeof(*sdr->thread_write.buffer2));
		if (!sdr->thread_write.buffer2) {
			LOGP(DSDR, LOGL_ERROR, "No mem!\n");
			goto error;
		}
		sdr->thread_write.in = sdr->thread_write.out = 0;
		if (oversample > 1) {
			iir_lowpass_init(&sdr->thread_write.lp[0], samplerate / 2.0, sdr_config->samplerate, 2);
			iir_lowpass_init(&sdr->thread_write.lp[1], samplerate / 2.0, sdr_config->samplerate, 2);
		}
	}

	/* alloc fm modulation buffers */
	sdr->modbuff = calloc(sdr->buffer_size * 2, sizeof(*sdr->modbuff));
	if (!sdr->modbuff) {
		LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
		goto error;
	}
	sdr->modbuff_I = calloc(sdr->buffer_size, sizeof(*sdr->modbuff_I));
	if (!sdr->modbuff_I) {
		LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
		goto error;
	}
	sdr->modbuff_Q = calloc(sdr->buffer_size, sizeof(*sdr->modbuff_Q));
	if (!sdr->modbuff_Q) {
		LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
		goto error;
	}
	sdr->modbuff_carrier = calloc(sdr->buffer_size, sizeof(*sdr->modbuff_carrier));
	if (!sdr->modbuff_carrier) {
		LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
		goto error;
	}
	sdr->wavespl0 = calloc(sdr->buffer_size, sizeof(*sdr->wavespl0));
	if (!sdr->wavespl0) {
		LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
		goto error;
	}
	sdr->wavespl1 = calloc(sdr->buffer_size, sizeof(*sdr->wavespl1));
	if (!sdr->wavespl1) {
		LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
		goto error;
	}

	/* special case where we use a paging frequency */
	if (paging_frequency) {
		/* add extra paging channel */
		sdr->paging_channel = channels;
	}

	/* create list of channel states */
	if (channels) {
		sdr->chan = calloc(sizeof(*sdr->chan), channels + (sdr->paging_channel != 0));
		if (!sdr->chan) {
			LOGP(DSDR, LOGL_ERROR, "NO MEM!\n");
			goto error;
		}
	}

	/* swap links, if required */
	if (sdr_config->swap_links) {
		double *temp;
		LOGP(DSDR, LOGL_NOTICE, "Sapping RX and TX frequencies!\n");
		temp = rx_frequency;
		rx_frequency = tx_frequency;
		tx_frequency = temp;
	}

	if (tx_frequency && !channels)
		tx_center_frequency = tx_frequency[0];
	if (tx_frequency && channels) {
		/* calculate required bandwidth (IQ rate) */

		double tx_low_frequency = 0.0, tx_high_frequency = 0.0;
		for (c = 0; c < channels; c++) {
			sdr->chan[c].tx_frequency = tx_frequency[c];
			if (c == 0 || sdr->chan[c].tx_frequency < tx_low_frequency)
				tx_low_frequency = sdr->chan[c].tx_frequency;
			if (c == 0 || sdr->chan[c].tx_frequency > tx_high_frequency)
				tx_high_frequency = sdr->chan[c].tx_frequency;
		}
		if (sdr->paging_channel) {
			sdr->chan[sdr->paging_channel].tx_frequency = paging_frequency;
			if (sdr->chan[sdr->paging_channel].tx_frequency < tx_low_frequency)
				tx_low_frequency = sdr->chan[sdr->paging_channel].tx_frequency;
			if (sdr->chan[sdr->paging_channel].tx_frequency > tx_high_frequency)
				tx_high_frequency = sdr->chan[sdr->paging_channel].tx_frequency;
		}
		tx_center_frequency = (tx_high_frequency + tx_low_frequency) / 2.0;

		/* prevent channel bandwidth from overlapping with the center frequency */
		if (channels == 1 && !sdr->paging_channel) {
			/* simple: just move off the center by two times half of the bandwidth */
			tx_center_frequency -= 2.0 * bandwidth / 2.0;
			/* Note: tx_low_frequency is kept at old center.
			   Calculation of 'low_side' will become 0.
			   This is correct, since there is no bandwidth
			   below new center frequency.
			 */
			LOGP(DSDR, LOGL_INFO, "We shift center frequency %.0f KHz down (half bandwidth), to prevent channel from overlapping with DC level.\n", bandwidth / 2.0 / 1e3);
		} else {
			/* find two channels that are aside the center */
			double low_dist = 0, high_dist = 0, dist;
			int low_c = -1, high_c = -1;
			for (c = 0; c < channels; c++) {
				dist = fabs(tx_center_frequency - sdr->chan[c].tx_frequency);
				if (round(sdr->chan[c].tx_frequency) >= round(tx_center_frequency)) {
					if (high_c < 0 || dist < high_dist) {
						high_dist = dist;
						high_c = c;
					}
				} else {
					if (low_c < 0 || dist < low_dist) {
						low_dist = dist;
						low_c = c;
					}
				}
			}
			if (sdr->paging_channel) {
				dist = fabs(tx_center_frequency - sdr->chan[sdr->paging_channel].tx_frequency);
				if (round(sdr->chan[sdr->paging_channel].tx_frequency) >= round(tx_center_frequency)) {
					if (high_c < 0 || dist < high_dist) {
						high_dist = dist;
						high_c = sdr->paging_channel;
					}
				} else {
					if (low_c < 0 || dist < low_dist) {
						low_dist = dist;
						low_c = sdr->paging_channel;
					}
				}
			}
			/* new center = center of the two frequencies aside old center */
			if (low_c >= 0 && high_c >= 0) {
				tx_center_frequency =
					((sdr->chan[low_c].tx_frequency) +
					 (sdr->chan[high_c].tx_frequency)) / 2.0;
				LOGP(DSDR, LOGL_INFO, "We move center frequency between the two channels in the middle, to prevent them from overlapping with DC level.\n");
			}
		}

		/* show spectrum */
		show_spectrum("TX", (double)samplerate / 2.0, tx_center_frequency, tx_frequency, paging_frequency, channels);

		/* range of TX */
		double low_side, high_side, range;
		low_side = (tx_center_frequency - tx_low_frequency) + bandwidth / 2.0;
		high_side = (tx_high_frequency - tx_center_frequency) + bandwidth / 2.0;
		range = ((low_side > high_side) ? low_side : high_side) * 2.0;
		LOGP(DSDR, LOGL_INFO, "Total bandwidth (two sidebands) for all TX Frequencies: %.0f Hz\n", range);
		if (range > samplerate * USABLE_BANDWIDTH) {
			LOGP(DSDR, LOGL_NOTICE, "*******************************************************************************\n");
			LOGP(DSDR, LOGL_NOTICE, "The required bandwidth of %.0f Hz exceeds %.0f%% of the sample rate.\n", range, USABLE_BANDWIDTH * 100.0);
			LOGP(DSDR, LOGL_NOTICE, "Please increase samplerate!\n");
			LOGP(DSDR, LOGL_NOTICE, "*******************************************************************************\n");
			goto error;
		}
		LOGP(DSDR, LOGL_INFO, "Using center frequency: TX %.6f MHz\n", tx_center_frequency / 1e6);
		/* set offsets to center frequency */
		for (c = 0; c < channels; c++) {
			double tx_offset;
			tx_offset = sdr->chan[c].tx_frequency - tx_center_frequency;
			LOGP(DSDR, LOGL_DEBUG, "Frequency #%d: TX offset: %.6f MHz\n", c, tx_offset / 1e6);
			sdr->chan[c].am = am[c];
			if (am[c]) {
				double gain, bias;
				gain = modulation_index / 2.0;
				bias = 1.0 - gain;
				rc = am_mod_init(&sdr->chan[c].am_mod, samplerate, tx_offset, sdr->amplitude * gain, sdr->amplitude * bias);
			} else
				rc = fm_mod_init(&sdr->chan[c].fm_mod, samplerate, tx_offset, sdr->amplitude);
			if (rc < 0)
				goto error;
		}
		if (sdr->paging_channel) {
			double tx_offset;
			tx_offset = sdr->chan[sdr->paging_channel].tx_frequency - tx_center_frequency;
			LOGP(DSDR, LOGL_DEBUG, "Paging Frequency: TX offset: %.6f MHz\n", tx_offset / 1e6);
			rc = fm_mod_init(&sdr->chan[sdr->paging_channel].fm_mod, samplerate, tx_offset, sdr->amplitude);
			if (rc < 0)
				goto error;
		}
		/* show gain */
		LOGP(DSDR, LOGL_INFO, "Using gain: TX %.1f dB\n", sdr_config->tx_gain);
		/* open wave */
		if (sdr_config->write_iq_tx_wave) {
			rc = wave_create_record(&sdr->wave_tx_rec, sdr_config->write_iq_tx_wave, samplerate, 2, 1.0);
			if (rc < 0) {
				LOGP(DSDR, LOGL_ERROR, "Failed to create WAVE recoding instance!\n");
				goto error;
			}
		}
		if (sdr_config->read_iq_tx_wave) {
			int two = 2;
			rc = wave_create_playback(&sdr->wave_tx_play, sdr_config->read_iq_tx_wave, &samplerate, &two, 1.0);
			if (rc < 0) {
				LOGP(DSDR, LOGL_ERROR, "Failed to create WAVE playback instance!\n");
				goto error;
			}
		}
	}

	if (rx_frequency && !channels)
		rx_center_frequency = rx_frequency[0];
	if (rx_frequency && channels) {
		/* calculate required bandwidth (IQ rate) */
		double rx_low_frequency = 0.0, rx_high_frequency = 0.0;
		for (c = 0; c < channels; c++) {
			sdr->chan[c].rx_frequency = rx_frequency[c];
			if (c == 0 || sdr->chan[c].rx_frequency < rx_low_frequency)
				rx_low_frequency = sdr->chan[c].rx_frequency;
			if (c == 0 || sdr->chan[c].rx_frequency > rx_high_frequency)
				rx_high_frequency = sdr->chan[c].rx_frequency;
		}
		rx_center_frequency = (rx_high_frequency + rx_low_frequency) / 2.0;

		/* prevent channel bandwidth from overlapping with the center frequency */
		if (channels == 1) {
			/* simple: just move off the center by two times half of the bandwidth */
			rx_center_frequency -= 2.0 * bandwidth / 2.0;
			/* Note: rx_low_frequency is kept at old center.
			   Calculation of 'low_side' will become 0.
			   This is correct, since there is no bandwidth
			   below new center frequency.
			 */
			LOGP(DSDR, LOGL_INFO, "We shift center frequency %.0f KHz down (half bandwidth), to prevent channel from overlapping with DC level.\n", bandwidth / 2.0 / 1e3);
		} else {
			/* find two channels that are aside the center */
			double low_dist, high_dist, dist;
			int low_c = -1, high_c = -1;
			for (c = 0; c < channels; c++) {
				dist = fabs(rx_center_frequency - sdr->chan[c].rx_frequency);
				if (round(sdr->chan[c].rx_frequency) >= round(rx_center_frequency)) {
					if (high_c < 0 || dist < high_dist) {
						high_dist = dist;
						high_c = c;
					}
				} else {
					if (low_c < 0 || dist < low_dist) {
						low_dist = dist;
						low_c = c;
					}
				}
			}
			/* new center = center of the two frequencies aside old center */
			if (low_c >= 0 && high_c >= 0) {
				rx_center_frequency =
					((sdr->chan[low_c].rx_frequency) +
					 (sdr->chan[high_c].rx_frequency)) / 2.0;
				LOGP(DSDR, LOGL_INFO, "We move center frequency between the two channels in the middle, to prevent them from overlapping with DC level.\n");
			}
		}

		/* show spectrum */
		show_spectrum("RX", (double)samplerate / 2.0, rx_center_frequency, rx_frequency, 0.0, channels);

		/* range of RX */
		double low_side, high_side, range;
		low_side = (rx_center_frequency - rx_low_frequency) + bandwidth / 2.0;
		high_side = (rx_high_frequency - rx_center_frequency) + bandwidth / 2.0;
		range = ((low_side > high_side) ? low_side : high_side) * 2.0;
		LOGP(DSDR, LOGL_INFO, "Total bandwidth (two sidebands) for all RX Frequencies: %.0f Hz\n", range);
		if (range > samplerate * USABLE_BANDWIDTH) {
			LOGP(DSDR, LOGL_NOTICE, "*******************************************************************************\n");
			LOGP(DSDR, LOGL_NOTICE, "The required bandwidth of %.0f Hz exceeds %.0f%% of the sample rate.\n", range, USABLE_BANDWIDTH * 100.0);
			LOGP(DSDR, LOGL_NOTICE, "Please increase samplerate!\n");
			LOGP(DSDR, LOGL_NOTICE, "*******************************************************************************\n");
			goto error;
		}
		LOGP(DSDR, LOGL_INFO, "Using center frequency: RX %.6f MHz\n", rx_center_frequency / 1e6);
		/* set offsets to center frequency */
		for (c = 0; c < channels; c++) {
			double rx_offset;
			rx_offset = sdr->chan[c].rx_frequency - rx_center_frequency;
			LOGP(DSDR, LOGL_DEBUG, "Frequency #%d: RX offset: %.6f MHz\n", c, rx_offset / 1e6);
			sdr->chan[c].am = am[c];
			if (am[c])
				rc = am_demod_init(&sdr->chan[c].am_demod, samplerate, rx_offset, bandwidth / 2.0, 1.0 / modulation_index); /* bandwidth is only one side band */
			else
				rc = fm_demod_init(&sdr->chan[c].fm_demod, samplerate, rx_offset, bandwidth); /* bandwidth are deviation and both sidebands */
			if (rc < 0)
				goto error;
		}
		/* show gain */
		LOGP(DSDR, LOGL_INFO, "Using gain: RX %.1f dB\n", sdr_config->rx_gain);
		/* open wave */
		if (sdr_config->write_iq_rx_wave) {
			rc = wave_create_record(&sdr->wave_rx_rec, sdr_config->write_iq_rx_wave, samplerate, 2, 1.0);
			if (rc < 0) {
				LOGP(DSDR, LOGL_ERROR, "Failed to create WAVE recoding instance!\n");
				goto error;
			}
		}
		if (sdr_config->read_iq_rx_wave) {
			int two = 2;
			rc = wave_create_playback(&sdr->wave_rx_play, sdr_config->read_iq_rx_wave, &samplerate, &two, 1.0);
			if (rc < 0) {
				LOGP(DSDR, LOGL_ERROR, "Failed to create WAVE playback instance!\n");
				goto error;
			}
		}
		/* init measurements display */
		for (c = 0; c < channels; c++) {
			sender_t *sender = get_sender_by_empfangsfrequenz(sdr->chan[c].rx_frequency);
			if (!sender)
				continue;
			sdr->chan[c].dmp_rf_level = display_measurements_add(&sender->dispmeas, "RF Level", "%.1f dB", DISPLAY_MEAS_AVG, DISPLAY_MEAS_LEFT, -96.0, 0.0, -INFINITY);
			if (!am[c]) {
				sdr->chan[c].dmp_freq_offset = display_measurements_add(&sender->dispmeas, "Freq. Offset", "%+.2f KHz", DISPLAY_MEAS_AVG, DISPLAY_MEAS_CENTER, -max_modulation / 1000.0 * 2.0, max_modulation / 1000.0 * 2.0, 0.0);
				sdr->chan[c].dmp_deviation = display_measurements_add(&sender->dispmeas, "Deviation", "%.2f KHz", DISPLAY_MEAS_PEAK2PEAK, DISPLAY_MEAS_LEFT, 0.0, max_deviation / 1000.0 * 1.5, max_deviation / 1000.0);
			}
		}
	}

	display_iq_init(samplerate);
	display_spectrum_init(samplerate, rx_center_frequency);

	LOGP(DSDR, LOGL_INFO, "Using local oscillator offset: %.0f Hz\n", sdr_config->lo_offset);

#ifdef HAVE_UHD
	if (sdr_config->uhd) {
		rc = uhd_open(sdr_config->channel, sdr_config->device_args, sdr_config->stream_args, sdr_config->tune_args, sdr_config->tx_antenna, sdr_config->rx_antenna, sdr_config->clock_source, tx_center_frequency, rx_center_frequency, sdr_config->lo_offset, sdr_config->samplerate, sdr_config->tx_gain, sdr_config->rx_gain, sdr_config->bandwidth, sdr_config->timestamps);
		if (rc)
			goto error;
	}
#endif

#ifdef HAVE_SOAPY
	if (sdr_config->soapy) {
		rc = soapy_open(sdr_config->channel, sdr_config->device_args, sdr_config->stream_args, sdr_config->tune_args, sdr_config->tx_antenna, sdr_config->rx_antenna, sdr_config->clock_source, tx_center_frequency, rx_center_frequency, sdr_config->lo_offset, sdr_config->samplerate, sdr_config->tx_gain, sdr_config->rx_gain, sdr_config->bandwidth, sdr_config->timestamps);
		if (rc)
			goto error;
	}
#endif

	return sdr;

error:
	sdr_close(sdr);
	return NULL;
}

double bias_I, bias_Q; /* calculated bias */
int bias_count = -1; /* number of calculations */

void calibrate_bias(void)
{
	bias_count = 0;
	bias_I = 0.0;
	bias_Q = 0.0;
}

static void sdr_bias(float *buffer, int count)
{
	int i;

	if (bias_count < sdr_config->samplerate) {
		for (i = 0; i < count; i++) {
			bias_I += *buffer++;
			bias_Q += *buffer++;
		}
		bias_count += count;
		if (bias_count >= sdr_config->samplerate) {
			bias_I /= bias_count;
			bias_Q /= bias_count;
			LOGP(DSDR, LOGL_INFO, "DC bias calibration finished.\n");
		}
	} else {
		for (i = 0; i < count; i++) {
			*buffer++ -= bias_I;
			*buffer++ -= bias_Q;
		}
	}
}

static void *sdr_write_child(void *arg)
{
	sdr_t *sdr = (sdr_t *)arg;
	int num;
	int fill, out;
	int s, ss, o;

	while (sdr->thread_write.running) {
		/* write to SDR */
		fill = (sdr->thread_write.in - sdr->thread_write.out + sdr->thread_write.buffer_size) % sdr->thread_write.buffer_size;
		num = fill / 2;
		if (num) {
#ifdef DEBUG_BUFFER
			printf("Thread found %d samples in write buffer and forwards them to SDR.\n", num);
#endif
			out = sdr->thread_write.out;
			for (s = 0, ss = 0; s < num; s++) {
				for (o = 0; o < sdr->oversample; o++) {
					sdr->thread_write.buffer2[ss++] = sdr->thread_write.buffer[out] * LIMIT_IQ_LEVEL;
					sdr->thread_write.buffer2[ss++] = sdr->thread_write.buffer[out + 1] * LIMIT_IQ_LEVEL;
				}
				out = (out + 2) % sdr->thread_write.buffer_size;
			}
			sdr->thread_write.out = out;
#ifndef DISABLE_FILTER
			/* filter spectrum */
			if (sdr->oversample > 1) {
				iir_process_baseband(&sdr->thread_write.lp[0], sdr->thread_write.buffer2, num * sdr->oversample);
				iir_process_baseband(&sdr->thread_write.lp[1], sdr->thread_write.buffer2 + 1, num * sdr->oversample);
			}
#endif
#ifdef HAVE_UHD
			if (sdr_config->uhd)
				uhd_send(sdr->thread_write.buffer2, num * sdr->oversample);
#endif
#ifdef HAVE_SOAPY
			if (sdr_config->soapy)
				soapy_send(sdr->thread_write.buffer2, num * sdr->oversample);
#endif
		}

		/* delay some time */
		usleep(sdr->interval * 1000.0);
	}

	LOGP(DSDR, LOGL_DEBUG, "Thread received exit!\n");
	sdr->thread_write.exit = 1;
	return NULL;
}

static void *sdr_read_child(void *arg)
{
	sdr_t *sdr = (sdr_t *)arg;
	int num, count = 0;
	int space, in;
	int s, ss;

	while (sdr->thread_read.running) {
		/* read from SDR */
		space = (sdr->thread_read.out - sdr->thread_read.in - 2 + sdr->thread_read.buffer_size) % sdr->thread_read.buffer_size;
		num = space / 2;
		if (num) {
#ifdef HAVE_UHD
			if (sdr_config->uhd)
				count = uhd_receive(sdr->thread_read.buffer2, num);
#endif
#ifdef HAVE_SOAPY
			if (sdr_config->soapy)
				count = soapy_receive(sdr->thread_read.buffer2, num);
#endif
			if (bias_count >= 0)
				sdr_bias(sdr->thread_read.buffer2, count);
			if (count > 0) {
#ifdef DEBUG_BUFFER
				printf("Thread read %d samples from SDR and writes them to read buffer.\n", count);
#endif
#ifndef DISABLE_FILTER
				/* filter spectrum */
				if (sdr->oversample > 1) {
					iir_process_baseband(&sdr->thread_read.lp[0], sdr->thread_read.buffer2, count);
					iir_process_baseband(&sdr->thread_read.lp[1], sdr->thread_read.buffer2 + 1, count);
				}
#endif
				in = sdr->thread_read.in;
				for (s = 0, ss = 0; s < count; s++) {
					sdr->thread_read.buffer[in++] = sdr->thread_read.buffer2[ss++];
					sdr->thread_read.buffer[in++] = sdr->thread_read.buffer2[ss++];
					in %= sdr->thread_read.buffer_size;
				}
				sdr->thread_read.in = in;
			}
		}

		/* delay some time */
		usleep(sdr->interval * 1000.0);
	}

	LOGP(DSDR, LOGL_DEBUG, "Thread received exit!\n");
	sdr->thread_read.exit = 1;
	return NULL;
}

/* start streaming */
int sdr_start(void *inst)
{
	sdr_t *sdr = (sdr_t *)inst;
	int rc = -EINVAL;

#ifdef HAVE_UHD
	if (sdr_config->uhd)
		rc = uhd_start();
#endif
#ifdef HAVE_SOAPY
	if (sdr_config->soapy)
		rc = soapy_start();
#endif
	if (rc < 0)
		return rc;

	if (sdr->threads) {
		int rc;
		pthread_t tid;
		char tname[64];

		LOGP(DSDR, LOGL_DEBUG, "Create threads!\n");
		sdr->thread_write.running = 1;
		sdr->thread_write.exit = 0;
		rc = pthread_create(&tid, NULL, sdr_write_child, inst);
		if (rc < 0) {
			sdr->thread_write.running = 0;
			LOGP(DSDR, LOGL_ERROR, "Failed to create thread!\n");
			return rc;
		}
		pthread_getname_np(tid, tname, sizeof(tname));
		strncat(tname, "-sdr_tx", sizeof(tname) - 7 - 1);
		tname[sizeof(tname) - 1] = '\0';
		pthread_setname_np(tid, tname);
		sdr->thread_read.running = 1;
		sdr->thread_read.exit = 0;
		rc = pthread_create(&tid, NULL, sdr_read_child, inst);
		if (rc < 0) {
			sdr->thread_read.running = 0;
			LOGP(DSDR, LOGL_ERROR, "Failed to create thread!\n");
			return rc;
		}
		pthread_getname_np(tid, tname, sizeof(tname));
		strncat(tname, "-sdr_rx", sizeof(tname) - 7 - 1);
		tname[sizeof(tname) - 1] = '\0';
		pthread_setname_np(tid, tname);
	}

	return 0;
}

void sdr_close(void *inst)
{
	sdr_t *sdr = (sdr_t *)inst;

	LOGP(DSDR, LOGL_DEBUG, "Close SDR device\n");

	if (sdr->threads) {
		if (sdr->thread_write.running) {
			LOGP(DSDR, LOGL_DEBUG, "Thread sending exit!\n");
			sdr->thread_write.running = 0;
			while (sdr->thread_write.exit == 0)
				usleep(1000);
		}
		if (sdr->thread_read.running) {
			LOGP(DSDR, LOGL_DEBUG, "Thread sending exit!\n");
			sdr->thread_read.running = 0;
			while (sdr->thread_read.exit == 0)
				usleep(1000);
		}
	}

	if (sdr->thread_read.buffer)
		free((void *)sdr->thread_read.buffer);
	if (sdr->thread_read.buffer2)
		free((void *)sdr->thread_read.buffer2);
	if (sdr->thread_write.buffer)
		free((void *)sdr->thread_write.buffer);
	if (sdr->thread_write.buffer2)
		free((void *)sdr->thread_write.buffer2);

#ifdef HAVE_UHD
	if (sdr_config->uhd)
		uhd_close();
#endif

#ifdef HAVE_SOAPY
	if (sdr_config->soapy)
		soapy_close();
#endif

	if (sdr) {
		free(sdr->modbuff);
		free(sdr->modbuff_I);
		free(sdr->modbuff_Q);
		free(sdr->modbuff_carrier);
		free(sdr->wavespl0);
		free(sdr->wavespl1);
		wave_destroy_record(&sdr->wave_rx_rec);
		wave_destroy_record(&sdr->wave_tx_rec);
		wave_destroy_playback(&sdr->wave_rx_play);
		wave_destroy_playback(&sdr->wave_tx_play);
		if (sdr->chan) {
			int c;

			for (c = 0; c < sdr->channels; c++) {
				fm_mod_exit(&sdr->chan[c].fm_mod);
				fm_demod_exit(&sdr->chan[c].fm_demod);
				am_mod_exit(&sdr->chan[c].am_mod);
				am_demod_exit(&sdr->chan[c].am_demod);
			}
			if (sdr->paging_channel)
				fm_mod_exit(&sdr->chan[sdr->paging_channel].fm_mod);
			free(sdr->chan);
		}
		free(sdr);
		sdr = NULL;
	}

	display_spectrum_exit();
}

static double get_time(void)
{
	static struct timespec tv;

	clock_gettime(CLOCK_REALTIME, &tv);

	return (double)tv.tv_sec + (double)tv.tv_nsec / 1000000000.0;
}

int sdr_write(void *inst, sample_t **samples, uint8_t **power, int num, enum paging_signal __attribute__((unused)) *paging_signal, int *on, int channels)
{
	sdr_t *sdr = (sdr_t *)inst;
	float *buff = NULL;
	int c, s, ss;
	int sent = 0;

	if (num > sdr->buffer_size) {
		fprintf(stderr, "exceeding maximum size given by sdr->buffer_size, please fix!\n");
		abort();
	}
	if (channels != sdr->channels && channels != 0) {
		LOGP(DSDR, LOGL_ERROR, "Invalid number of channels, please fix!\n");
		abort();
	}

	/* process all channels */
	if (channels) {
		buff = sdr->modbuff;
		memset(buff, 0, sizeof(*buff) * num * 2);
		for (c = 0; c < channels; c++) {
			/* switch to paging channel, if requested */
			if (on[c] && sdr->paging_channel)
				fm_modulate_complex(&sdr->chan[sdr->paging_channel].fm_mod, samples[c], power[c], num, buff);
			else if (sdr->chan[c].am) {
				am_modulate_complex(&sdr->chan[c].am_mod, samples[c], power[c], num, buff);
			} else
				fm_modulate_complex(&sdr->chan[c].fm_mod, samples[c], power[c], num, buff);
		}
	} else {
		buff = (float *)samples;
	}

	if (sdr->wave_tx_rec.fp) {
		sample_t *spl_list[2] = { sdr->wavespl0, sdr->wavespl1 };
		for (s = 0, ss = 0; s < num; s++) {
			spl_list[0][s] = buff[ss++];
			spl_list[1][s] = buff[ss++];
		}
		wave_write(&sdr->wave_tx_rec, spl_list, num);
	}
	if (sdr->wave_tx_play.fp) {
		sample_t *spl_list[2] = { sdr->wavespl0, sdr->wavespl1 };
		wave_read(&sdr->wave_tx_play, spl_list, num);
		for (s = 0, ss = 0; s < num; s++) {
			buff[ss++] = spl_list[0][s];
			buff[ss++] = spl_list[1][s];
		}
	}

	if (sdr->threads) {
		/* store data towards SDR in ring buffer */
		int fill, space, in;

		fill = (sdr->thread_write.in - sdr->thread_write.out + sdr->thread_write.buffer_size) % sdr->thread_write.buffer_size;
		space = (sdr->thread_write.out - sdr->thread_write.in - 2 + sdr->thread_write.buffer_size) % sdr->thread_write.buffer_size;

		/* debug fill level */
		if (fill > sdr->thread_write.max_fill)
			sdr->thread_write.max_fill = fill;
		if (sdr->thread_write.max_fill_timer == 0.0)
			sdr->thread_write.max_fill_timer = get_time();
		if (get_time() - sdr->thread_write.max_fill_timer > 1.0) {
			double delay;
			delay = (double)sdr->thread_write.max_fill / 2.0 / (double)sdr->samplerate;
			sdr->thread_write.max_fill = 0;
			sdr->thread_write.max_fill_timer += 1.0;
			LOGP(DSDR, LOGL_DEBUG, "write delay = %.3f ms\n", delay * 1000.0);
		}

		if (space < num * 2) {
			LOGP(DSDR, LOGL_ERROR, "Write SDR buffer overflow!\n");
			num = space / 2;
		}
#ifdef DEBUG_BUFFER
		printf("Writing %d samples to write buffer.\n", num);
#endif
		in = sdr->thread_write.in;
		for (s = 0, ss = 0; s < num; s++) {
			sdr->thread_write.buffer[in++] = buff[ss++];
			sdr->thread_write.buffer[in++] = buff[ss++];
			in %= sdr->thread_write.buffer_size;
		}
		sdr->thread_write.in = in;
		sent = num;
	} else {
#ifdef HAVE_UHD
		if (sdr_config->uhd)
			sent = uhd_send(buff, num);
#endif
#ifdef HAVE_SOAPY
		if (sdr_config->soapy)
			sent = soapy_send(buff, num);
#endif
		if (sent < 0)
			return sent;
	}
	
	return sent;
}

int sdr_read(void *inst, sample_t **samples, int num, int channels, double *rf_level_db)
{
	sdr_t *sdr = (sdr_t *)inst;
	float *buff = NULL;
	int count = 0;
	int c, s, ss;

	if (num > sdr->buffer_size) {
		fprintf(stderr, "exceeding maximum size given by sdr->buffer_size, please fix!\n");
		abort();
	}

	if (channels) {
		buff = sdr->modbuff;
	} else {
		buff = (float *)samples;
	}

	if (sdr->threads) {
		/* load data from SDR out of ring buffer */
		int fill, out;

		fill = (sdr->thread_read.in - sdr->thread_read.out + sdr->thread_read.buffer_size) % sdr->thread_read.buffer_size;

		/* debug fill level */
		if (fill > sdr->thread_read.max_fill)
			sdr->thread_read.max_fill = fill;
		if (sdr->thread_read.max_fill_timer == 0.0)
			sdr->thread_read.max_fill_timer = get_time();
		if (get_time() - sdr->thread_read.max_fill_timer > 1.0) {
			double delay;
			delay = (double)sdr->thread_read.max_fill / 2.0 / (double)sdr_config->samplerate;
			sdr->thread_read.max_fill = 0;
			sdr->thread_read.max_fill_timer += 1.0;
			LOGP(DSDR, LOGL_DEBUG, "read delay = %.3f ms\n", delay * 1000.0);
		}

		if (fill / 2 / sdr->oversample < num)
			num = fill / 2 / sdr->oversample;
#ifdef DEBUG_BUFFER
		printf("Reading %d samples from read buffer.\n", num);
#endif
		out = sdr->thread_read.out;
		for (s = 0, ss = 0; s < num; s++) {
			buff[ss++] = sdr->thread_read.buffer[out];
			buff[ss++] = sdr->thread_read.buffer[out + 1];
			out = (out + 2 * sdr->oversample) % sdr->thread_read.buffer_size;
		}
		sdr->thread_read.out = out;
		count = num;
	} else {
#ifdef HAVE_UHD
		if (sdr_config->uhd)
			count = uhd_receive(buff, num);
#endif
#ifdef HAVE_SOAPY
		if (sdr_config->soapy)
			count = soapy_receive(buff, num);
#endif
		if (bias_count >= 0)
			sdr_bias(buff, count);
		if (count <= 0)
			return count;
	}

	if (sdr_rx_overflow) {
		LOGP(DSDR, LOGL_ERROR, "SDR RX overflow!\n");
		sdr_rx_overflow = 0;
	}

	if (sdr->wave_rx_rec.fp) {
		sample_t *spl_list[2] = { sdr->wavespl0, sdr->wavespl1 };
		for (s = 0, ss = 0; s < count; s++) {
			spl_list[0][s] = buff[ss++];
			spl_list[1][s] = buff[ss++];
		}
		wave_write(&sdr->wave_rx_rec, spl_list, count);
	}
	if (sdr->wave_rx_play.fp) {
		sample_t *spl_list[2] = { sdr->wavespl0, sdr->wavespl1 };
		wave_read(&sdr->wave_rx_play, spl_list, count);
		for (s = 0, ss = 0; s < count; s++) {
			buff[ss++] = spl_list[0][s];
			buff[ss++] = spl_list[1][s];
		}
	}
	display_iq(buff, count);
	display_spectrum(buff, count);

	if (channels) {
		for (c = 0; c < channels; c++) {
			if (rf_level_db)
				rf_level_db[c] = NAN;
			if (sdr->chan[c].am)
				am_demodulate_complex(&sdr->chan[c].am_demod, samples[c], count, buff, sdr->modbuff_I, sdr->modbuff_Q, sdr->modbuff_carrier);
			else
				fm_demodulate_complex(&sdr->chan[c].fm_demod, samples[c], count, buff, sdr->modbuff_I, sdr->modbuff_Q);
			sender_t *sender = get_sender_by_empfangsfrequenz(sdr->chan[c].rx_frequency);
			if (!sender || !count)
				continue;
			double min, max, avg;
			avg = 0.0;
			for (s = 0; s < count; s++) {
				/* average the square length of vector */
				avg += sdr->modbuff_I[s] * sdr->modbuff_I[s] + sdr->modbuff_Q[s] * sdr->modbuff_Q[s];
			}
			avg = sqrt(avg /(double)count); /* RMS */
			avg = log10(avg) * 20;
			display_measurements_update(sdr->chan[c].dmp_rf_level, avg, 0.0);
			if (rf_level_db)
				rf_level_db[c] = avg;
			if (!sdr->chan[c].am) {
				min = 0.0;
				max = 0.0;
				avg = 0.0;
				for (s = 0; s < count; s++) {
					avg += samples[c][s];
					if (s == 0 || samples[c][s] > max)
						max = samples[c][s];
					if (s == 0 || samples[c][s] < min)
						min = samples[c][s];
				}
				avg /= (double)count;
				display_measurements_update(sdr->chan[c].dmp_freq_offset, avg / 1000.0, 0.0);
				/* use half min and max, because we want the deviation above/below (+-) center frequency. */
				display_measurements_update(sdr->chan[c].dmp_deviation, min / 2.0 / 1000.0, max / 2.0 / 1000.0);
			}
		}
	}

	return count;
}

/* how much do we need to send (in audio sample duration) to get the target delay (buffer size) */
int sdr_get_tosend(void *inst, int buffer_size)
{
	sdr_t *sdr = (sdr_t *)inst;
	int count = 0;

#ifdef HAVE_UHD
	if (sdr_config->uhd)
		count = uhd_get_tosend(buffer_size * sdr->oversample);
#endif
#ifdef HAVE_SOAPY
	if (sdr_config->soapy)
		count = soapy_get_tosend(buffer_size * sdr->oversample);
#endif
	if (count < 0)
		return count;
	/* rounding down, so we never overfill */
	count /= sdr->oversample;

	if (sdr->threads) {
		/* subtract what we have in write buffer, because this is not jet sent to the SDR */
		int fill;

		fill = (sdr->thread_write.in - sdr->thread_write.out + sdr->thread_write.buffer_size) % sdr->thread_write.buffer_size;
		count -= fill / 2;
		if (count < 0)
			count = 0;
	}

	return count;
}


