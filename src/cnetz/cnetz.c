/* C-Netz protocol handling
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
#include <errno.h>
#include <math.h>
#include "../common/debug.h"
#include "../common/timer.h"
#include "../common/call.h"
#include "../common/cause.h"
#include "cnetz.h"
#include "sysinfo.h"
#include "telegramm.h"
#include "dsp.h"

/* uncomment this to do echo debugging (-L) on Speech Channel */
//#define DEBUG_SPK

/* Call reference for calls from mobile station to network
   This offset of 0x400000000 is required for MNCC interface. */
static int new_callref = 0x40000000;

/* Convert channel number to frequency number of base station.
   Set 'unterband' to 1 to get frequency of mobile station. */
double cnetz_kanal2freq(int kanal, int unterband)
{
	double freq = 465.750;

	if ((kanal & 1))
		freq -= (double)(kanal + 1) / 2.0 * 0.010;
	else
		freq -= (double)kanal / 2.0 * 0.0125;
	if (unterband)
		freq -= 10.0;

	return freq;
}

/* Convert ISDN cause to 'Ausloesegrund' of C-Netz mobile station */
uint8_t cnetz_cause_isdn2cnetz(int cause)
{
	switch (cause) {
	case CAUSE_NORMAL:
	case CAUSE_BUSY:
	case CAUSE_NOANSWER:
		return CNETZ_CAUSE_TEILNEHMERBESETZT;
	case CAUSE_OUTOFORDER:
	case CAUSE_INVALNUMBER:
	case CAUSE_NOCHANNEL:
	case CAUSE_TEMPFAIL:
	default:
		return CNETZ_CAUSE_GASSENBESETZT;
	}
}

/* global init */
int cnetz_init(void)
{
	return 0;
}

static void cnetz_go_idle(cnetz_t *cnetz);
static transaction_t *create_transaction(cnetz_t *cnetz, uint32_t state, uint8_t futln_nat, uint8_t futln_fuvst, uint16_t futln_rest);
static transaction_t *search_transaction(cnetz_t *cnetz, uint32_t state_mask);
static void destroy_transaction(transaction_t *trans);
static void trans_new_state(transaction_t *trans, int state);
static void cnetz_flush_other_transactions(cnetz_t *cnetz, transaction_t *trans);

/* Create transceiver instance and link to a list. */
int cnetz_create(const char *sounddev, int samplerate, int pre_emphasis, int de_emphasis, const char *write_wave, const char *read_wave, int kanal, int auth, int ms_power, int measure_speed, double clock_speed[2], double deviation, double noise, int loopback)
{
	cnetz_t *cnetz;
	int rc;

	if ((kanal & 1) && kanal < 1 && kanal > 947) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "Channel ('Kanal') number %d invalid.\n", kanal);
		return -EINVAL;
	}
	if (!(kanal & 1) && kanal < 2 && kanal > 758) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "Channel ('Kanal') number %d invalid.\n", kanal);
		return -EINVAL;
	}
	if (kanal == 1 || kanal == 2) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Channel ('Kanal') number %d is specified as 'unused', it might not work!\n", kanal);
	}

	if (kanal == CNETZ_OGK_KANAL) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "You selected channel %d ('Orga-Kanal') for speech channel. Some phones will reject this.\n", CNETZ_OGK_KANAL);
	}

	cnetz = calloc(1, sizeof(cnetz_t));
	if (!cnetz) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "No memory!\n");
		return -ENOMEM;
	}

	PDEBUG(DCNETZ, DEBUG_DEBUG, "Creating 'C-Netz' instance for 'Kanal' = %d (sample rate %d).\n", kanal, samplerate);

	/* init general part of transceiver */
	/* do not enable emphasis, since it is done by cnetz code, not by common sender code */
	rc = sender_create(&cnetz->sender, sounddev, samplerate, 0, 0, write_wave, read_wave, kanal, loopback, 0, -1);
	if (rc < 0) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "Failed to init transceiver process!\n");
		goto error;
	}

	/* init audio processing */
	rc = dsp_init_sender(cnetz, measure_speed, clock_speed, deviation, noise);
	if (rc < 0) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "Failed to init signal processing!\n");
		goto error;
	}

	cnetz->auth = auth;
	cnetz->ms_power = ms_power;

	cnetz->pre_emphasis = pre_emphasis;
	cnetz->de_emphasis = de_emphasis;
	rc = init_emphasis(&cnetz->estate, samplerate);
	if (rc < 0)
		goto error;

	/* go into idle state */
	cnetz->dsp_mode = DSP_MODE_OGK;
	cnetz->sched_dsp_mode = DSP_MODE_OGK;
	cnetz->sched_switch_mode = 0;
	cnetz_go_idle(cnetz);

#ifdef DEBUG_SPK
	transaction_t *trans = create_transaction(cnetz, TRANS_DS, 2, 2, 22002);
	trans->mo_call = 1;
	cnetz->sched_switch_mode = 2;
	cnetz->sched_dsp_mode = DSP_MODE_SPK_K;
#endif

	return 0;

error:
	cnetz_destroy(&cnetz->sender);

	return rc;
}

/* Destroy transceiver instance and unlink from list. */
void cnetz_destroy(sender_t *sender)
{
	cnetz_t *cnetz = (cnetz_t *) sender;
	transaction_t *trans;

	PDEBUG(DCNETZ, DEBUG_DEBUG, "Destroying 'C-Netz' instance for 'Kanal' = %d.\n", sender->kanal);

	while ((trans = search_transaction(cnetz, ~0))) {
		const char *rufnummer = transaction2rufnummer(trans);
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Removing pending transaction for subscriber '%s'\n", rufnummer);
		destroy_transaction(trans);
	}

	dsp_cleanup_sender(cnetz);
	sender_destroy(&cnetz->sender);
	free(cnetz);
}

/* Abort connection, if any and send idle broadcast */
static void cnetz_go_idle(cnetz_t *cnetz)
{
	if (cnetz->sender.callref) {
		PDEBUG(DBNETZ, DEBUG_ERROR, "Releasing missing callref, please fix!\n");
		call_in_release(cnetz->sender.callref, CAUSE_NORMAL);
		cnetz->sender.callref = 0;
	}

	/* set scheduler to OgK */
	PDEBUG(DBNETZ, DEBUG_INFO, "Entering IDLE state, sending 'Funkzellenkennung' %d,%d,%d.\n", si.fuz_nat, si.fuz_fuvst, si.fuz_rest);
	cnetz->state = CNETZ_IDLE;
	if (cnetz->dsp_mode == DSP_MODE_SPK_K || cnetz->dsp_mode == DSP_MODE_SPK_V) {
		/* go idle after next frame/slot */
		cnetz->sched_switch_mode = 1;
		cnetz->sched_dsp_mode = DSP_MODE_OGK;
	} else {
		cnetz->sched_switch_mode = 0;
		cnetz->dsp_mode = DSP_MODE_OGK;
	}
}

/* Initiate release connection on speech channel */
static void cnetz_release(transaction_t *trans, uint8_t cause)
{
	trans_new_state(trans, TRANS_AF);
	trans->release_cause = cause;
	trans->cnetz->sched_switch_mode = 0;
	trans->count = 0;
	timer_stop(&trans->timer);
}

/* Receive audio from call instance. */
void call_rx_audio(int callref, int16_t *samples, int count)
{
	sender_t *sender;
	cnetz_t *cnetz;

	for (sender = sender_head; sender; sender = sender->next) {
		cnetz = (cnetz_t *) sender;
		if (sender->callref == callref)
			break;
	}
	if (!sender)
		return;

	if (cnetz->dsp_mode == DSP_MODE_SPK_V) {
		/* store as is, since we convert rate when processing FSK frames */
		jitter_save(&cnetz->sender.audio, samples, count);
	}
}

int call_out_setup(int callref, char *dialing)
{
	sender_t *sender;
	cnetz_t *cnetz;
	transaction_t *trans;
	uint8_t futln_nat;
	uint8_t futln_fuvst;
	uint16_t futln_rest;
	int i;

	/* 1. check if number is invalid, return INVALNUMBER */
	if (strlen(dialing) == 11 && !strncmp(dialing, "0160", 4))
		dialing += 4;
	if (strlen(dialing) != 7) {
inval:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Outgoing call to invalid number '%s', rejecting!\n", dialing);
		return -CAUSE_INVALNUMBER;
	}
	for (i = 0; i < 7; i++) {
		if (dialing[i] < '0' || dialing[i] > '9')
			goto inval;
	}
	if (atoi(dialing + 2) > 65535) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Last 5 digits '%s' must not exceed '65535', but they do!\n", dialing + 2);
		goto inval;
	}

	futln_nat = dialing[0] - '0';
	futln_fuvst = dialing[1] - '0';
	futln_rest = atoi(dialing + 2);

	/* 2. check if given number is already in a call, return BUSY */
	for (sender = sender_head; sender; sender = sender->next) {
		cnetz = (cnetz_t *) sender;
		/* search transaction for this number */
		trans = cnetz->trans_list;
		while (trans) {
			if (trans->futln_nat == futln_nat
			 && trans->futln_fuvst == futln_fuvst
			 && trans->futln_rest == futln_rest)
				break;
			trans = trans->next;
		}
		if (trans)
			break;
	}
	if (sender) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Outgoing call to busy number, rejecting!\n");
		return -CAUSE_BUSY;
	}

	/* 3. check if all senders are busy, return NOCHANNEL */
	for (sender = sender_head; sender; sender = sender->next) {
		cnetz = (cnetz_t *) sender;
		if (cnetz->state == CNETZ_IDLE)
			break;
	}
	if (!sender) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Outgoing call, but no free channel, rejecting!\n");
		return -CAUSE_NOCHANNEL;
	}

	PDEBUG(DCNETZ, DEBUG_INFO, "Call to mobile station, paging station id '%s'\n", dialing);

	/* 4. trying to page mobile station */
	sender->callref = callref;

	trans = create_transaction(cnetz, TRANS_VAK, dialing[0] - '0', dialing[1] - '0', atoi(dialing + 2));
	if (!trans) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "Failed to create transaction\n");
		sender->callref = 0;
		return -CAUSE_TEMPFAIL;
	}
	cnetz->state = CNETZ_BUSY;
	/* flush all other transactions, if any */
	cnetz_flush_other_transactions(cnetz, trans);

	return 0;
}

/* Call control sends disconnect (with tones).
 * An active call stays active, so tones and annoucements can be received
 * by mobile station.
 */
void call_out_disconnect(int callref, int cause)
{
	sender_t *sender;
	cnetz_t *cnetz;
	transaction_t *trans;

	PDEBUG(DCNETZ, DEBUG_INFO, "Call has been disconnected by network.\n");

	for (sender = sender_head; sender; sender = sender->next) {
		cnetz = (cnetz_t *) sender;
		if (sender->callref == callref)
			break;
	}
	if (!sender) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Outgoing disconnect, but no callref!\n");
		call_in_release(callref, CAUSE_INVALCALLREF);
		return;
	}

	if (cnetz->state != CNETZ_BUSY) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Outgoing release, but sender is not in busy state.\n");
		call_in_release(callref, cause);
		sender->callref = 0;
		return;
	}

	trans = cnetz->trans_list;
	if (!trans) {
		call_in_release(callref, cause);
		sender->callref = 0;
		return;
	}

	/* Release when not active */

	switch (cnetz->dsp_mode) {
	case DSP_MODE_SPK_V:
		return;
	case DSP_MODE_SPK_K:
		PDEBUG(DCNETZ, DEBUG_INFO, "Call control disconnects on speech channel, releasing towards mobile station.\n");
		cnetz_release(trans, cnetz_cause_isdn2cnetz(cause));
		break;
	default:
		PDEBUG(DCNETZ, DEBUG_INFO, "Call control disconnects on organisation channel, removing transaction.\n");
		destroy_transaction(trans);
		cnetz_go_idle(cnetz);
	}

	call_in_release(callref, cause);

	sender->callref = 0;
}

/* Call control releases call toward mobile station. */
void call_out_release(int callref, int cause)
{
	sender_t *sender;
	cnetz_t *cnetz;
	transaction_t *trans;

	PDEBUG(DCNETZ, DEBUG_INFO, "Call has been released by network, releasing call.\n");

	for (sender = sender_head; sender; sender = sender->next) {
		cnetz = (cnetz_t *) sender;
		if (sender->callref == callref)
			break;
	}
	if (!sender) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Outgoing release, but no callref!\n");
		/* don't send release, because caller already released */
		return;
	}

	sender->callref = 0;

	if (cnetz->state != CNETZ_BUSY) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Outgoing release, but sender is not in busy state.\n");
		return;
	}

	trans = cnetz->trans_list;
	if (!trans)
		return;

	switch (cnetz->dsp_mode) {
	case DSP_MODE_SPK_K:
	case DSP_MODE_SPK_V:
		PDEBUG(DCNETZ, DEBUG_INFO, "Call control releases on speech channel, releasing towards mobile station.\n");
		cnetz_release(trans, cnetz_cause_isdn2cnetz(cause));
		break;
	default:
		PDEBUG(DCNETZ, DEBUG_INFO, "Call control releases on organisation channel, removing transaction.\n");
		destroy_transaction(trans);
		cnetz_go_idle(cnetz);
	}
}

/*
 * Transaction handling
 */

static void transaction_timeout(struct timer *timer);

/* create transaction */
static transaction_t *create_transaction(cnetz_t *cnetz, uint32_t state, uint8_t futln_nat, uint8_t futln_fuvst, uint16_t futln_rest)
{
	transaction_t *trans, **transp;

	/* search transaction for this subsriber */
	trans = cnetz->trans_list;
	while (trans) {
		if (trans->futln_nat == futln_nat
		 && trans->futln_fuvst == futln_fuvst
		 && trans->futln_rest == futln_rest) {
			const char *rufnummer = transaction2rufnummer(trans);
			PDEBUG(DCNETZ, DEBUG_NOTICE, "Found alredy pending transaction for subscriber '%s', deleting!\n", rufnummer);
			destroy_transaction(trans);
			break;
		}
		trans = trans->next;
	}

	trans = calloc(1, sizeof(*trans));
	if (!trans) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "No memory!\n");
		return NULL;
	}

	timer_init(&trans->timer, transaction_timeout, trans);

	trans_new_state(trans, state);
	trans->futln_nat = futln_nat;
	trans->futln_fuvst = futln_fuvst;
	trans->futln_rest = futln_rest;

	if (state == TRANS_VWG)
		trans->mo_call = 1;
	if (state == TRANS_VAK)
		trans->mt_call = 1;

	const char *rufnummer = transaction2rufnummer(trans);
	PDEBUG(DCNETZ, DEBUG_INFO, "Created transaction for subscriber '%s'\n", rufnummer);

	/* attach to end of list, so first transaction is served first */
	trans->cnetz = cnetz;
	transp = &cnetz->trans_list;
	while (*transp)
		transp = &((*transp)->next);
	*transp = trans;

	return trans;
}

/* destroy transaction */
static void destroy_transaction(transaction_t *trans)
{
	transaction_t **transp;

	/* unlink */
	transp = &trans->cnetz->trans_list;
	while (*transp && *transp != trans)
		transp = &((*transp)->next);
	if (!(*transp)) {
		PDEBUG(DCNETZ, DEBUG_ERROR, "Transaction not in list, please fix!!\n");
		abort();
	}
	*transp = trans->next;
	
	const char *rufnummer = transaction2rufnummer(trans);
	PDEBUG(DCNETZ, DEBUG_INFO, "Destroying transaction for subscriber '%s'\n", rufnummer);

	timer_exit(&trans->timer);

	trans_new_state(trans, 0);

	free(trans);
}

static transaction_t *search_transaction(cnetz_t *cnetz, uint32_t state_mask)
{
	transaction_t *trans = cnetz->trans_list;

	while (trans) {
		if ((trans->state & state_mask)) {
			const char *rufnummer = transaction2rufnummer(trans);
			PDEBUG(DCNETZ, DEBUG_DEBUG, "Found transaction for subscriber '%s'\n", rufnummer);
			return trans;
		}
		trans = trans->next;
	}

	return NULL;
}

static const char *trans_state_name(int state)
{
	switch (state) {
	case 0:
		return "IDLE";
	case TRANS_EM:
		return "EM";
	case TRANS_UM:
		return "UM";
	case TRANS_MA:
		return "MA";
	case TRANS_VWG:
		return "VWG";
	case TRANS_WAF:
		return "WAF";
	case TRANS_WBP:
		return "WBP";
	case TRANS_WBN:
		return "WBN";
	case TRANS_VAG:
		return "VAG";
	case TRANS_VAK:
		return "VAK";
	case TRANS_BQ:
		return "BQ";
	case TRANS_VHQ:
		return "VHQ";
	case TRANS_RTA:
		return "RTA";
	case TRANS_DS:
		return "DS";
	case TRANS_AHQ:
		return "AHQ";
	case TRANS_AF:
		return "AF";
	case TRANS_AT:
		return "AT";
	default:
		return "<invald transaction state>";
	}
}

static void trans_new_state(transaction_t *trans, int state)
{
	PDEBUG(DCNETZ, DEBUG_INFO, "Transaction state %s -> %s\n", trans_state_name(trans->state), trans_state_name(state));
	trans->state = state;
}

/* Timeout handling */
static void transaction_timeout(struct timer *timer)
{
	transaction_t *trans = (transaction_t *)timer->priv;
	cnetz_t *cnetz = trans->cnetz;

	switch (trans->state) {
	case TRANS_WAF:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "No response after dialing request 'Wahlaufforderung'\n");
		if (++trans->count == 3) {
			trans_new_state(trans, TRANS_WBN);
			break;
		}
		trans_new_state(trans, TRANS_VWG);
		break;
	case TRANS_BQ:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "No response after channel allocation 'Belegung Quittung'\n");
		if (trans->mt_call) {
			call_in_release(cnetz->sender.callref, CAUSE_OUTOFORDER);
			cnetz->sender.callref = 0;
		}
		cnetz_release(trans, CNETZ_CAUSE_FUNKTECHNISCH);
		break;
	case TRANS_VHQ:
		if (cnetz->dsp_mode != DSP_MODE_SPK_V)
			PDEBUG(DCNETZ, DEBUG_NOTICE, "No response hile holding call 'Quittung Verbindung halten'\n");
		else
			PDEBUG(DCNETZ, DEBUG_NOTICE, "Lost signal from 'FuTln' (mobile station)\n");
		if (trans->mt_call || trans->mo_call) {
			call_in_release(cnetz->sender.callref, CAUSE_TEMPFAIL);
			cnetz->sender.callref = 0;
		}
		cnetz_release(trans, CNETZ_CAUSE_FUNKTECHNISCH);
		break;
	case TRANS_DS:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "No response after connect 'Durchschalten'\n");
		call_in_release(cnetz->sender.callref, CAUSE_TEMPFAIL);
		cnetz->sender.callref = 0;
		cnetz_release(trans, CNETZ_CAUSE_FUNKTECHNISCH);
		break;
	case TRANS_RTA:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "No response after ringing order 'Rufton anschalten'\n");
		call_in_release(cnetz->sender.callref, CAUSE_TEMPFAIL);
		cnetz->sender.callref = 0;
		cnetz_release(trans, CNETZ_CAUSE_FUNKTECHNISCH);
		break;
	case TRANS_AHQ:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "No response after answer 'Abhebequittung'\n");
		call_in_release(cnetz->sender.callref, CAUSE_TEMPFAIL);
		cnetz->sender.callref = 0;
		cnetz_release(trans, CNETZ_CAUSE_FUNKTECHNISCH);
		break;
	default:
		PDEBUG(DCNETZ, DEBUG_ERROR, "Timeout unhandled in state %d\n", trans->state);
	}
}

static void cnetz_flush_other_transactions(cnetz_t *cnetz, transaction_t *trans)
{
	/* flush after this very trans */
	while (trans->next) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Kicking other pending transaction\n");
		destroy_transaction(trans);
	}
	/* flush before this very trans */
	while (cnetz->trans_list != trans) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Kicking other pending transaction\n");
		destroy_transaction(cnetz->trans_list);
	}
}

/*
 * sync to phone
 *
 * because we don't know the actual delay on sound card, we need to sync
 * to the phone, that is synced to us.
 *
 * if block is given, we can set sync to absolute position in super frame.
 * if not, we just sync to the nearest block.
 */

void cnetz_sync_frame(cnetz_t *cnetz, double sync, int block)
{
	double offset;

	if (block >= 0) {
		/* offset is the actual sync relative to bit_time */
		offset = fmod(sync - BITS_PER_BLOCK * (double)block + BITS_PER_SUPERFRAME, BITS_PER_SUPERFRAME);
		if (offset > BITS_PER_SUPERFRAME / 2)
			offset -= BITS_PER_SUPERFRAME;
	} else {
		/* sync to the nearest block */
		/* offset is the actual sync relative to bit_time */
		offset = fmod(sync, BITS_PER_BLOCK);
		if (offset > BITS_PER_BLOCK / 2)
			offset -= BITS_PER_BLOCK;
	}
	/* if more than +- one bit out of sync */
	if (offset < -0.5 || offset > 0.5) {
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Frame sync offset = %.2f, correcting!\n", offset);
		fsk_correct_sync(cnetz, offset);
		return;
	}

	/* resync by some fraction of received sync error */
	PDEBUG(DCNETZ, DEBUG_DEBUG, "Frame sync offset = %.2f, correcting.\n", offset);
	fsk_correct_sync(cnetz, offset / 2.0);
}

/*
 * OgK handling
 */

/* transmit rufblock */
const telegramm_t *cnetz_transmit_telegramm_rufblock(cnetz_t *cnetz)
{
	static telegramm_t telegramm;
	transaction_t *trans;

	memset(&telegramm, 0, sizeof(telegramm));

	telegramm.opcode = OPCODE_LR_R;
	telegramm.max_sendeleistung = cnetz->ms_power;
	telegramm.bedingte_genauigkeit_der_fufst = si.genauigkeit;
	telegramm.zeitschlitz_nr = cnetz->sched_ts;
	telegramm.grenzwert_fuer_einbuchen_und_umbuchen = si.grenz_einbuchen;
	telegramm.authentifikationsbit = cnetz->auth;
	telegramm.vermittlungstechnische_sperren = si.sperre;
	telegramm.ws_kennung = 0;
	telegramm.reduzierungsfaktor = si.reduzierung;
	telegramm.fuz_nationalitaet = si.fuz_nat;
	telegramm.fuz_fuvst_nr = si.fuz_fuvst;
	telegramm.fuz_rest_nr = si.fuz_rest;
	telegramm.kennung_fufst = si.fufst_prio;
	telegramm.nachbarschafts_prioritaets_bit = si.nachbar_prio;
	telegramm.bewertung_nach_pegel_und_entfernung = si.bewertung;
	telegramm.entfernungsangabe_der_fufst = si.entfernung;
	telegramm.mittelungsfaktor_fuer_ausloesen = si.mittel_ausloesen;
	telegramm.mittelungsfaktor_fuer_umschalten = si.mittel_umschalten;
	telegramm.grenzwert_fuer_umschalten = si.grenz_umschalten;
	telegramm.grenze_fuer_ausloesen = si.grenz_ausloesen;
	
	trans = search_transaction(cnetz, TRANS_EM | TRANS_UM | TRANS_WBN | TRANS_WBP | TRANS_VAG | TRANS_VAK);
	if (trans) {
		telegramm.futln_nationalitaet = trans->futln_nat;
		telegramm.futln_heimat_fuvst_nr = trans->futln_fuvst;
		telegramm.futln_rest_nr = trans->futln_rest;
		switch (trans->state) {
		case TRANS_EM:
			PDEBUG(DCNETZ, DEBUG_INFO, "Sending acknowledgement 'Einbuchquittung' to Attachment request.\n");
			telegramm.opcode = OPCODE_EBQ_R;
			destroy_transaction(trans);
			break;
		case TRANS_UM:
			PDEBUG(DCNETZ, DEBUG_INFO, "Sending acknowledgement 'Umbuchquittung' to Roaming requuest.\n");
			telegramm.opcode = OPCODE_UBQ_R;
			destroy_transaction(trans);
			break;
		case TRANS_WBN:
			PDEBUG(DCNETZ, DEBUG_INFO, "Sending call reject 'Wahlbestaetigung negativ'.\n");
			telegramm.opcode = OPCODE_WBN_R;
			destroy_transaction(trans);
			cnetz_go_idle(cnetz);
			break;
		case TRANS_WBP:
			PDEBUG(DCNETZ, DEBUG_INFO, "Sending call accept 'Wahlbestaetigung positiv'.\n");
			telegramm.opcode = OPCODE_WBP_R;
			trans_new_state(trans, TRANS_VAG);
			break;
		case TRANS_VAG:
		case TRANS_VAK:
			if (trans->state == TRANS_VAG) {
				PDEBUG(DCNETZ, DEBUG_INFO, "Sending channel assignment 'Verbindungsaufbau gehend'.\n");
				telegramm.opcode = OPCODE_VAG_R;
			} else {
				PDEBUG(DCNETZ, DEBUG_INFO, "Sending channel assignment 'Verbindungsaufbau kommend'.\n");
				telegramm.opcode = OPCODE_VAK_R;
			}
			telegramm.frequenz_nr = cnetz->sender.kanal;
			trans_new_state(trans, TRANS_BQ);
			trans->count = 0;
			timer_start(&trans->timer, 0.150 + 0.0375 * F_BQ); /* two slots + F_BQ frames */
			/* schedule switching two slots ahead */
			cnetz->sched_switch_mode = 2;
			cnetz->sched_dsp_mode = DSP_MODE_SPK_K;
			break;
		default:
			; /* LR */
		}
	}

	return &telegramm;
}

/* transmit meldeblock */
const telegramm_t *cnetz_transmit_telegramm_meldeblock(cnetz_t *cnetz)
{
	static telegramm_t telegramm;
	transaction_t *trans;

	memset(&telegramm, 0, sizeof(telegramm));
	telegramm.opcode = OPCODE_MLR_M;
	telegramm.max_sendeleistung = cnetz->ms_power;
	telegramm.ogk_verkehrsanteil = 0; /* must be 0 or phone might not respond to messages in different slot */
	telegramm.teilnehmersperre = 0;
	telegramm.anzahl_gesperrter_teilnehmergruppen = 0;
	telegramm.ogk_vorschlag = CNETZ_OGK_KANAL;
	telegramm.fuz_rest_nr = si.fuz_rest;

	trans = search_transaction(cnetz, TRANS_VWG);
	if (trans) {
		switch (trans->state) {
		case TRANS_VWG:
			PDEBUG(DCNETZ, DEBUG_INFO, "Sending acknowledgement 'Wahlaufforderung' to outging call\n");
			telegramm.opcode = OPCODE_WAF_M;
			telegramm.futln_nationalitaet = trans->futln_nat;
			telegramm.futln_heimat_fuvst_nr = trans->futln_fuvst;
			telegramm.futln_rest_nr = trans->futln_rest;
			trans_new_state(trans, TRANS_WAF);
			timer_start(&trans->timer, 4.0); /* Wait two slot cycles until resending */
			break;
		default:
			; /* MLR */
		}
	}

	return &telegramm;
}

void cnetz_receive_telegramm_ogk(cnetz_t *cnetz, telegramm_t *telegramm, int block)
{
	uint8_t opcode = telegramm->opcode;
	int valid_frame = 0;
	transaction_t *trans;
	const char *rufnummer;

	switch (opcode) {
	case OPCODE_EM_R:
		if (!match_fuz(telegramm))
			break;
		rufnummer = telegramm2rufnummer(telegramm);
		if (cnetz->auth && telegramm->chipkarten_futelg_bit)
			PDEBUG(DCNETZ, DEBUG_INFO, "Received Attachment 'Einbuchen' message from Subscriber '%s' with chip card's ID %d (vendor id %d, hardware version %d, software version %d)\n", rufnummer, telegramm->kartenkennung, telegramm->herstellerkennung, telegramm->hardware_des_futelg, telegramm->software_des_futelg);
		else
			PDEBUG(DCNETZ, DEBUG_INFO, "Received Attachment 'Einbuchen' message from Subscriber '%s' with %s card's security code %d\n", rufnummer, (telegramm->chipkarten_futelg_bit) ? "chip":"magnet", telegramm->sicherungs_code);
		if (cnetz->state != CNETZ_IDLE) {
			PDEBUG(DCNETZ, DEBUG_NOTICE, "Ignoring Attachment from subscriber '%s', because we are busy.\n", rufnummer);
			break;
		}
		trans = create_transaction(cnetz, TRANS_EM, telegramm->futln_nationalitaet, telegramm->futln_heimat_fuvst_nr, telegramm->futln_rest_nr);
		if (!trans) {
			PDEBUG(DCNETZ, DEBUG_ERROR, "Failed to create transaction\n");
			break;
		}
		valid_frame = 1;
		break;
	case OPCODE_UM_R:
		if (!match_fuz(telegramm))
			break;
		rufnummer = telegramm2rufnummer(telegramm);
		if (cnetz->auth && telegramm->chipkarten_futelg_bit)
			PDEBUG(DCNETZ, DEBUG_INFO, "Received Roaming 'Umbuchen' message from Subscriber '%s' with chip card's ID %d (vendor id %d, hardware version %d, software version %d)\n", rufnummer, telegramm->kartenkennung, telegramm->herstellerkennung, telegramm->hardware_des_futelg, telegramm->software_des_futelg);
		else
			PDEBUG(DCNETZ, DEBUG_INFO, "Received Roaming 'Umbuchen' message from Subscriber '%s' with %s card's security code %d\n", rufnummer, (telegramm->chipkarten_futelg_bit) ? "chip":"magnet", telegramm->sicherungs_code);
		if (cnetz->state != CNETZ_IDLE) {
			PDEBUG(DCNETZ, DEBUG_NOTICE, "Ignoring Roaming from subscriber '%s', because we are busy.\n", rufnummer);
			break;
		}
		trans = create_transaction(cnetz, TRANS_UM, telegramm->futln_nationalitaet, telegramm->futln_heimat_fuvst_nr, telegramm->futln_rest_nr);
		if (!trans) {
			PDEBUG(DCNETZ, DEBUG_ERROR, "Failed to create transaction\n");
			break;
		}
		valid_frame = 1;
		break;
	case OPCODE_VWG_R:
	case OPCODE_SRG_R:
		if (!match_fuz(telegramm))
			break;
		rufnummer = telegramm2rufnummer(telegramm);
		PDEBUG(DCNETZ, DEBUG_INFO, "Received outgoing Call 'Verbindungswunsch gehend' message from Subscriber '%s'\n", rufnummer);
		if (cnetz->state != CNETZ_IDLE) {
			PDEBUG(DCNETZ, DEBUG_NOTICE, "Ignoring Call from subscriber '%s', because we are busy.\n", rufnummer);
			break;
		}
		trans = create_transaction(cnetz, TRANS_VWG, telegramm->futln_nationalitaet, telegramm->futln_heimat_fuvst_nr, telegramm->futln_rest_nr);
		if (!trans) {
			PDEBUG(DCNETZ, DEBUG_ERROR, "Failed to create transaction\n");
			break;
		}
		cnetz->state = CNETZ_BUSY;
		/* flush all other transactions, if any */
		cnetz_flush_other_transactions(cnetz, trans);
		valid_frame = 1;
		break;
	case OPCODE_WUE_M:
		trans = search_transaction(cnetz, TRANS_WAF | TRANS_WBP | TRANS_VAG);
		if (!trans) {
			PDEBUG(DCNETZ, DEBUG_NOTICE, "Received dialing digits 'Wahluebertragung' message without transaction, ignoring!\n");
			break;
		}
		rufnummer = transaction2rufnummer(trans);
		strncpy(trans->dialing, telegramm->wahlziffern, sizeof(trans->dialing) - 1);
		PDEBUG(DCNETZ, DEBUG_INFO, "Received dialing digits 'Wahluebertragung' message from Subscriber '%s' to Number '%s'\n", rufnummer, trans->dialing);
		timer_stop(&trans->timer);
		trans_new_state(trans, TRANS_WBP);
		valid_frame = 1;
		break;
	default:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Received unexpected Telegramm (opcode %d = %s)\n", opcode, telegramm_name(opcode));
	}

	if (cnetz->sender.loopback) {
		fprintf(stderr, "we don't know TS here, but we are in loopback mode. in loopback mode call to this function shall never happen. please fix or find a way to know when the time slot was received!\n");
		abort();
	}

	if (valid_frame)
		cnetz_sync_frame(cnetz, telegramm->sync_time, block);
}

/*
 * SpK handling
 */

/* transmit concentrated messages */
const telegramm_t *cnetz_transmit_telegramm_spk_k(cnetz_t *cnetz)
{
	static telegramm_t telegramm;
	transaction_t *trans = cnetz->trans_list;

	memset(&telegramm, 0, sizeof(telegramm));
	if (!trans)
		return &telegramm;

	telegramm.max_sendeleistung = cnetz->ms_power;
	telegramm.sendeleistungsanpassung = 1;
	telegramm.entfernung = si.entfernung;
	telegramm.fuz_nationalitaet = si.fuz_nat;
	telegramm.fuz_fuvst_nr = si.fuz_fuvst;
	telegramm.fuz_rest_nr = si.fuz_rest;
	telegramm.futln_nationalitaet = trans->futln_nat;
	telegramm.futln_heimat_fuvst_nr = trans->futln_fuvst;
	telegramm.futln_rest_nr = trans->futln_rest;
	telegramm.frequenz_nr = cnetz->sender.kanal;
	telegramm.bedingte_genauigkeit_der_fufst = si.genauigkeit;

	switch (trans->state) {
	case TRANS_BQ:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Belegungsquittung' on traffic channel\n");
		telegramm.opcode = OPCODE_BQ_K;
		if (++trans->count >= 8 && !timer_running(&trans->timer)) {
			trans_new_state(trans, TRANS_VHQ);
			trans->count = 0;
			timer_start(&trans->timer, 0.0375 * F_VHQK); /* F_VHQK frames */
		}
		break;
	case TRANS_VHQ:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Quittung Verbindung halten' on traffic channel\n");
		telegramm.opcode = OPCODE_VHQ_K;
		if ((cnetz->sched_ts & 7) == 7 && cnetz->sched_r_m && !timer_running(&trans->timer)) {
			/* next sub frame */
			if (trans->mo_call) {
				int callref = ++new_callref;
				int rc;
				rc = call_in_setup(callref, transaction2rufnummer(trans), trans->dialing);
				if (rc < 0) {
					PDEBUG(DCNETZ, DEBUG_NOTICE, "Call rejected (cause %d), releasing.\n", -rc);
					cnetz_release(trans, cnetz_cause_isdn2cnetz(-rc));
					goto call_failed;
				}
				cnetz->sender.callref = callref;
				trans_new_state(trans, TRANS_DS);
				trans->count = 0;
				timer_start(&trans->timer, 0.0375 * F_DS); /* F_DS frames */
			}
			if (trans->mt_call) {
				trans_new_state(trans, TRANS_RTA);
				timer_start(&trans->timer, 0.0375 * F_RTA); /* F_RTA frames */
				trans->count = 0;
				call_in_alerting(cnetz->sender.callref);
			}
		}
		break;
	case TRANS_DS:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Durchschalten' on traffic channel\n");
		telegramm.opcode = OPCODE_DSB_K;
		if ((cnetz->sched_ts & 7) == 7 && cnetz->sched_r_m && !timer_running(&trans->timer)) {
			/* next sub frame */
			trans_new_state(trans, TRANS_VHQ);
			trans->count = 0;
			cnetz->sched_switch_mode = 1;
			cnetz->sched_dsp_mode = DSP_MODE_SPK_V;
#ifndef DEBUG_SPK
			timer_start(&trans->timer, 0.075 + 0.6 * F_VHQ); /* one slot + F_VHQ frames */
#endif
		}
		break;
	case TRANS_RTA:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Rufton anschalten' on traffic channel\n");
		telegramm.opcode = OPCODE_RTA_K;
		break;
	case TRANS_AHQ:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Abhebe Quittung' on traffic channel\n");
		telegramm.opcode = OPCODE_AHQ_K;
		if ((cnetz->sched_ts & 7) == 7 && cnetz->sched_r_m) {
			/* next sub frame */
			trans_new_state(trans, TRANS_VHQ);
			trans->count = 0;
			cnetz->sched_switch_mode = 1;
			cnetz->sched_dsp_mode = DSP_MODE_SPK_V;
			timer_start(&trans->timer, 0.075 + 0.6 * F_VHQ); /* one slot + F_VHQ frames */
		}
		break;
	case TRANS_AF:
call_failed:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Ausloesen durch FuFSt' on traffic channel\n");
		telegramm.opcode = OPCODE_AF_K;
		if (++trans->count == N_AFKT) {
			destroy_transaction(trans);
			cnetz_go_idle(cnetz);
		}
		break;
	case TRANS_AT:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Auslosen durch FuTln' on traffic channel\n");
		telegramm.opcode = OPCODE_AF_K;
		if (++trans->count == 1) {
			destroy_transaction(trans);
			cnetz_go_idle(cnetz);
		}
		break;
	}

	return &telegramm;
}

/* receive concentrated messages */
void cnetz_receive_telegramm_spk_k(cnetz_t *cnetz, telegramm_t *telegramm)
{
	uint8_t opcode = telegramm->opcode;
	int valid_frame = 0;
	transaction_t *trans = cnetz->trans_list;

	if (!trans)
		return;

	switch (opcode) {
	case OPCODE_BEL_K:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		PDEBUG(DCNETZ, DEBUG_INFO, "Received allocation 'Belegung' message.\n");
		valid_frame = 1;
		if (trans->state != TRANS_BQ)
			break;
		timer_stop(&trans->timer);
		break;
	case OPCODE_DSQ_K:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		PDEBUG(DCNETZ, DEBUG_INFO, "Received assignment confirm 'Durchschaltung Quittung' message.\n");
		valid_frame = 1;
		if (trans->state != TRANS_DS)
			break;
		cnetz->scrambler = telegramm->betriebs_art;
		timer_stop(&trans->timer);
		break;
	case OPCODE_VH_K:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		PDEBUG(DCNETZ, DEBUG_INFO, "Received connection hold 'Verbindung halten' message.\n");
		valid_frame = 1;
		if (trans->state != TRANS_VHQ)
			break;
		timer_stop(&trans->timer);
		break;
	case OPCODE_RTAQ_K:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		valid_frame = 1;
		PDEBUG(DCNETZ, DEBUG_INFO, "Received ringback 'Rufton anschlaten Quittung' message.\n");
		if (trans->state != TRANS_RTA)
			break;
		timer_start(&trans->timer, 0.0375 * F_RTA); /* F_RTA frames */
		break;
	case OPCODE_AH_K:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		PDEBUG(DCNETZ, DEBUG_INFO, "Received answer frame 'Abheben' message.\n");
		valid_frame = 1;
		/* if already received this frame, or if we are already on VHQ or if we are releasing */
		if (trans->state == TRANS_AHQ || trans->state == TRANS_VHQ || trans->state == TRANS_AF)
			break;
		cnetz->scrambler = telegramm->betriebs_art;
		trans_new_state(trans, TRANS_AHQ);
		trans->count = 0;
		timer_stop(&trans->timer);
		call_in_answer(cnetz->sender.callref, transaction2rufnummer(trans));
		break;
	case OPCODE_AT_K:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		PDEBUG(DCNETZ, DEBUG_INFO, "Received release frame 'Ausloesen durch FuTln' message.\n");
		valid_frame = 1;
		/* if already received this frame, if we are releasing */
		if (trans->state == TRANS_AT || trans->state == TRANS_AF)
			break;
		trans_new_state(trans, TRANS_AT);
		trans->count = 0;
		timer_stop(&trans->timer);
		if (cnetz->sender.callref) {
			call_in_release(cnetz->sender.callref, CAUSE_TEMPFAIL);
			cnetz->sender.callref = 0;
		}
		break;
	default:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Received unexpected Telegramm (opcode %d = %s)\n", opcode, telegramm_name(opcode));
	}

	if (valid_frame)
		cnetz_sync_frame(cnetz, telegramm->sync_time, -1);
}

/* transmit distributed messages */
const telegramm_t *cnetz_transmit_telegramm_spk_v(cnetz_t *cnetz)
{
	static telegramm_t telegramm;
	transaction_t *trans = cnetz->trans_list;

	memset(&telegramm, 0, sizeof(telegramm));
	if (!trans)
		return &telegramm;

	telegramm.max_sendeleistung = cnetz->ms_power;
	telegramm.sendeleistungsanpassung = 1;
	telegramm.ankuendigung_gespraechsende = 0;
	telegramm.gebuehren_stand = 0;
	telegramm.fuz_nationalitaet = si.fuz_nat;
	telegramm.fuz_fuvst_nr = si.fuz_fuvst;
	telegramm.fuz_rest_nr = si.fuz_rest;
	telegramm.futln_nationalitaet = trans->futln_nat;
	telegramm.futln_heimat_fuvst_nr = trans->futln_fuvst;
	telegramm.futln_rest_nr = trans->futln_rest;
	telegramm.frequenz_nr = cnetz->sender.kanal;
	telegramm.entfernung = si.entfernung;
	telegramm.bedingte_genauigkeit_der_fufst = si.genauigkeit;
	telegramm.gueltigkeit_des_gebuehrenstandes = 0;
	telegramm.ausloesegrund = trans->release_cause;

	switch (trans->state) {
	case TRANS_VHQ:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Quittung Verbindung halten' on traffic channel\n");
		if ((cnetz->sched_ts & 8) == 0) /* sub frame 1 and 3 */
			telegramm.opcode = OPCODE_VHQ1_V;
		else /* sub frame 2 and 4 */
			telegramm.opcode = OPCODE_VHQ2_V;
		break;
	case TRANS_AF:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Ausloesen durch FuFSt' on traffic channel\n");
		telegramm.opcode = OPCODE_AF_V;
		if (++trans->count == N_AFV) {
			destroy_transaction(trans);
			cnetz_go_idle(cnetz);
		}
		break;
	case TRANS_AT:
		PDEBUG(DCNETZ, DEBUG_INFO, "Sending 'Auslosen durch FuTln' on traffic channel\n");
		telegramm.opcode = OPCODE_AF_V;
		if (++trans->count == 1) {
			destroy_transaction(trans);
			cnetz_go_idle(cnetz);
		}
		break;
	}

	return &telegramm;
}

/* receive distributed messages */
void cnetz_receive_telegramm_spk_v(cnetz_t *cnetz, telegramm_t *telegramm)
{
	uint8_t opcode = telegramm->opcode;
	int valid_frame = 0;
	transaction_t *trans = cnetz->trans_list;

	if (!trans)
		return;

	switch (opcode) {
	case OPCODE_VH_V:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		if (trans->state != TRANS_VHQ)
			break;
		timer_start(&trans->timer, 0.6 * F_VHQ); /* F_VHQ frames */
		PDEBUG(DCNETZ, DEBUG_INFO, "Received supervisory frame 'Verbindung halten' message.\n");
		valid_frame = 1;
		cnetz->scrambler = telegramm->betriebs_art;
		break;
	case OPCODE_AT_V:
		if (!match_fuz(telegramm)) {
			break;
		}
		if (!match_futln(telegramm, trans->futln_nat, trans->futln_fuvst, trans->futln_rest)) {
			break;
		}
		PDEBUG(DCNETZ, DEBUG_INFO, "Received release frame 'Ausloesen durch FuTln' message.\n");
		valid_frame = 1;
		/* if already received this frame, if we are releasing */
		if (trans->state == TRANS_AT || trans->state == TRANS_AF)
			break;
		cnetz->scrambler = telegramm->betriebs_art;
		trans_new_state(trans, TRANS_AT);
		trans->count = 0;
		timer_stop(&trans->timer);
		if (cnetz->sender.callref) {
			call_in_release(cnetz->sender.callref, CAUSE_TEMPFAIL);
			cnetz->sender.callref = 0;
		}
		break;
	default:
		PDEBUG(DCNETZ, DEBUG_NOTICE, "Received unexpected Telegramm (opcode %d = %s)\n", opcode, telegramm_name(opcode));
	}

	if (valid_frame)
		cnetz_sync_frame(cnetz, telegramm->sync_time, -1);
}
