/* Osmo-CC: Message handling
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include "../libdebug/debug.h"
#include "message.h"

static uint32_t new_callref = 0;

uint32_t osmo_cc_new_callref(void)
{
	return (++new_callref);
}

const char *osmo_cc_msg_name(uint8_t msg_type)
{
	switch (msg_type) {
	case OSMO_CC_MSG_SETUP_REQ:
		return "CC-SETUP-REQ";
	case OSMO_CC_MSG_SETUP_IND:
		return "CC-SETUP-IND";
	case OSMO_CC_MSG_REJ_REQ:
		return "CC-REJ-REQ";
	case OSMO_CC_MSG_REJ_IND:
		return "CC-REJ-IND";
	case OSMO_CC_MSG_SETUP_ACK_REQ:
		return "CC-SETUP-ACK-REQ";
	case OSMO_CC_MSG_SETUP_ACK_IND:
		return "CC-SETUP-ACK-IND";
	case OSMO_CC_MSG_PROC_REQ:
		return "CC-PROC-REQ";
	case OSMO_CC_MSG_PROC_IND:
		return "CC-PROC-IND";
	case OSMO_CC_MSG_ALERT_REQ:
		return "CC-ALERT-REQ";
	case OSMO_CC_MSG_ALERT_IND:
		return "CC-ALERT-IND";
	case OSMO_CC_MSG_SETUP_RSP:
		return "CC-SETUP-RSP";
	case OSMO_CC_MSG_SETUP_CNF:
		return "CC-SETUP-CNF";
	case OSMO_CC_MSG_SETUP_COMP_REQ:
		return "CC-SETUP-COMP-REQ";
	case OSMO_CC_MSG_SETUP_COMP_IND:
		return "CC-SETUP-COMP-IND";
	case OSMO_CC_MSG_DISC_REQ:
		return "CC-DISC-REQ";
	case OSMO_CC_MSG_DISC_IND:
		return "CC-DISC-IND";
	case OSMO_CC_MSG_REL_REQ:
		return "CC-REL-REQ";
	case OSMO_CC_MSG_REL_CNF:
		return "CC-REL-CNF";
	case OSMO_CC_MSG_REL_IND:
		return "CC-REL-IND";
	case OSMO_CC_MSG_PROGRESS_REQ:
		return "CC-PROGRESS-REQ";
	case OSMO_CC_MSG_PROGRESS_IND:
		return "CC-PROGRESS-IND";
	case OSMO_CC_MSG_NOTIFY_REQ:
		return "CC-NOTIFY-REQ";
	case OSMO_CC_MSG_NOTIFY_IND:
		return "CC-NOTIFY-IND";
	case OSMO_CC_MSG_INFO_REQ:
		return "CC-INFO-REQ";
	case OSMO_CC_MSG_INFO_IND:
		return "CC-INFO-IND";
	case OSMO_CC_MSG_ATTACH_REQ:
		return "CC-ATTACH-REQ";
	case OSMO_CC_MSG_ATTACH_IND:
		return "CC-ATTACH-IND";
	case OSMO_CC_MSG_ATTACH_RSP:
		return "CC-ATTACH-RSP";
	case OSMO_CC_MSG_ATTACH_CNF:
		return "CC-ATTACH-CNF";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_ie_name(uint8_t ie_type)
{
	switch (ie_type) {
	case OSMO_CC_IE_CALLED:
		return "IE_CALLED";
	case OSMO_CC_IE_CALLED_SUB:
		return "IE_CALLED_SUB";
	case OSMO_CC_IE_CALLED_NAME:
		return "IE_CALLED_NAME";
	case OSMO_CC_IE_CALLED_INTERFACE:
		return "IE_CALLED_INTERFACE";
	case OSMO_CC_IE_DTMF:
		return "IE_DTMF";
	case OSMO_CC_IE_KEYPAD:
		return "IE_KEYPAD";
	case OSMO_CC_IE_COMPLETE:
		return "IE_COMPLETE";
	case OSMO_CC_IE_CALLING:
		return "IE_CALLING";
	case OSMO_CC_IE_CALLING_SUB:
		return "IE_CALLING_SUB";
	case OSMO_CC_IE_CALLING_NAME:
		return "IE_CALLING_NAME";
	case OSMO_CC_IE_CALLING_INTERFACE:
		return "IE_CALLING_INTERFACE";
	case OSMO_CC_IE_CALLING_NETWORK:
		return "IE_CALLING_NETWORK";
	case OSMO_CC_IE_REDIR:
		return "IE_REDIR";
	case OSMO_CC_IE_PROGRESS:
		return "IE_PROGRESS";
	case OSMO_CC_IE_NOTIFY:
		return "IE_NOTIFY";
	case OSMO_CC_IE_DISPLAY:
		return "IE_DISPLAY";
	case OSMO_CC_IE_CAUSE:
		return "IE_CAUSE";
	case OSMO_CC_IE_BEARER:
		return "IE_BEARER";
	case OSMO_CC_IE_SDP:
		return "IE_SDP";
	case OSMO_CC_IE_SOCKET_ADDRESS:
		return "IE_SOCKET_ADDRESS";
	case OSMO_CC_IE_PRIVATE:
		return "IE_PRIVATE";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_number_type_name(uint8_t type)
{
	switch (type) {
	case OSMO_CC_TYPE_UNKNOWN:
		return "unknown";
	case OSMO_CC_TYPE_INTERNATIONAL:
		return "international";
	case OSMO_CC_TYPE_NATIONAL:
		return "national";
	case OSMO_CC_TYPE_NETWORK:
		return "network";
	case OSMO_CC_TYPE_SUBSCRIBER:
		return "subscriber";
	case OSMO_CC_TYPE_ABBREVIATED:
		return "abbreviated";
	case OSMO_CC_TYPE_RESERVED:
		return "reserved";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_number_plan_name(uint8_t plan)
{
	switch (plan) {
	case OSMO_CC_PLAN_UNKNOWN:
		return "unknown";
	case OSMO_CC_PLAN_TELEPHONY:
		return "telephony";
	case OSMO_CC_PLAN_DATA:
		return "data";
	case OSMO_CC_PLAN_TTY:
		return "tty";
	case OSMO_CC_PLAN_NATIONAL_STANDARD:
		return "national standard";
	case OSMO_CC_PLAN_PRIVATE:
		return "private";
	case OSMO_CC_PLAN_RESERVED:
		return "reserved";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_number_present_name(uint8_t present)
{
	switch (present) {
	case OSMO_CC_PRESENT_ALLOWED:
		return "allowed";
	case OSMO_CC_PRESENT_RESTRICTED:
		return "restricted";
	case OSMO_CC_PRESENT_NOT_AVAIL:
		return "not available";
	case OSMO_CC_PRESENT_RESERVED:
		return "reserved";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_number_screen_name(uint8_t screen)
{
	switch (screen) {
	case OSMO_CC_SCREEN_USER_UNSCREENED:
		return "unscreened";
	case OSMO_CC_SCREEN_USER_VERIFIED_PASSED:
		return "user provided and passed";
	case OSMO_CC_SCREEN_USER_VERIFIED_FAILED:
		return "user provided an failed";
	case OSMO_CC_SCREEN_NETWORK:
		return "network provided";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_redir_reason_name(uint8_t reason)
{
	switch (reason) {
	case OSMO_CC_REDIR_REASON_UNKNOWN:
		return "unknown";
	case OSMO_CC_REDIR_REASON_CFB:
		return "call forward busy";
	case OSMO_CC_REDIR_REASON_CFNR:
		return "call forward no response";
	case OSMO_CC_REDIR_REASON_CD:
		return "call deflect";
	case OSMO_CC_REDIR_REASON_CF_OUTOFORDER:
		return "call forward out of order";
	case OSMO_CC_REDIR_REASON_CF_BY_DTE:
		return "call froward by dte";
	case OSMO_CC_REDIR_REASON_CFU:
		return "call forward unconditional";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_notify_name(uint8_t notify)
{
	switch (notify) {
	case OSMO_CC_NOTIFY_USER_SUSPENDED:
		return "user suspended";
	case OSMO_CC_NOTIFY_USER_RESUMED:
		return "user resumed";
	case OSMO_CC_NOTIFY_BEARER_SERVICE_CHANGE:
		return "bearer service change";
	case OSMO_CC_NOTIFY_CALL_COMPLETION_DELAY:
		return "call completion delay";
	case OSMO_CC_NOTIFY_CONFERENCE_ESTABLISHED:
		return "conference established";
	case OSMO_CC_NOTIFY_CONFERENCE_DISCONNECTED:
		return "conference disconnected";
	case OSMO_CC_NOTIFY_OTHER_PARTY_ADDED:
		return "ohter party added";
	case OSMO_CC_NOTIFY_ISOLATED:
		return "isolated";
	case OSMO_CC_NOTIFY_REATTACHED:
		return "reattached";
	case OSMO_CC_NOTIFY_OTHER_PARTY_ISOLATED:
		return "ohter party isolated";
	case OSMO_CC_NOTIFY_OTHER_PARTY_REATTACHED:
		return "ohter party reattached";
	case OSMO_CC_NOTIFY_OTHER_PARTY_SPLIT:
		return "other party split";
	case OSMO_CC_NOTIFY_OTHER_PARTY_DISCONNECTED:
		return "other party disconnected";
	case OSMO_CC_NOTIFY_CONFERENCE_FLOATING:
		return "confernce floating";
	case OSMO_CC_NOTIFY_CONFERENCE_DISC_PREEMPT:
		return "confernce disconnect preemption";
	case OSMO_CC_NOTIFY_CONFERENCE_FLOATING_SUP:
		return "conference floating sup";
	case OSMO_CC_NOTIFY_CALL_IS_A_WAITING_CALL:
		return "call is a waiting call";
	case OSMO_CC_NOTIFY_DIVERSION_ACTIVATED:
		return "diversion activated";
	case OSMO_CC_NOTIFY_RESERVED_CT_1:
		return "reserved CT 1";
	case OSMO_CC_NOTIFY_RESERVED_CT_2:
		return "reserved CT 2";
	case OSMO_CC_NOTIFY_REVERSE_CHARGING:
		return "reverse charging";
	case OSMO_CC_NOTIFY_REMOTE_HOLD:
		return "remote hold";
	case OSMO_CC_NOTIFY_REMOTE_RETRIEVAL:
		return "remote retrieval";
	case OSMO_CC_NOTIFY_CALL_IS_DIVERTING:
		return "call is diverting";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_coding_name(uint8_t coding)
{
	switch (coding) {
	case OSMO_CC_CODING_ITU_T:
		return "ITU-T";
	case OSMO_CC_CODING_ISO_IEC:
		return "ISO/IEC";
	case OSMO_CC_CODING_NATIONAL:
		return "national";
	case OSMO_CC_CODING_STANDARD_SPECIFIC:
		return "standard specific";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_isdn_cause_name(uint8_t cause)
{
	switch (cause) {
	case 0:
		return "unset";
	case OSMO_CC_ISDN_CAUSE_UNASSIGNED_NR:
		return "unsassigned number";
	case OSMO_CC_ISDN_CAUSE_NO_ROUTE_TRANSIT:
		return "no route to transit network";
	case OSMO_CC_ISDN_CAUSE_NO_ROUTE:
		return "no route";
	case OSMO_CC_ISDN_CAUSE_CHAN_UNACCEPT:
		return "channel unacceptable";
	case OSMO_CC_ISDN_CAUSE_OP_DET_BARRING:
		return "detected barring";
	case OSMO_CC_ISDN_CAUSE_NORM_CALL_CLEAR:
		return "normal call clearing";
	case OSMO_CC_ISDN_CAUSE_USER_BUSY:
		return "user busy";
	case OSMO_CC_ISDN_CAUSE_USER_NOTRESPOND:
		return "user not responding";
	case OSMO_CC_ISDN_CAUSE_USER_ALERTING_NA:
		return "user does not answer";
	case OSMO_CC_ISDN_CAUSE_CALL_REJECTED:
		return "call rejected";
	case OSMO_CC_ISDN_CAUSE_NUMBER_CHANGED:
		return "number changed";
	case OSMO_CC_ISDN_CAUSE_PRE_EMPTION:
		return "pre-emption";
	case OSMO_CC_ISDN_CAUSE_NONSE_USER_CLR:
		return "non-selected user clearing";
	case OSMO_CC_ISDN_CAUSE_DEST_OOO:
		return "destination out-of-order";
	case OSMO_CC_ISDN_CAUSE_INV_NR_FORMAT:
		return "invalid number format";
	case OSMO_CC_ISDN_CAUSE_FACILITY_REJ:
		return "facility rejected";
	case OSMO_CC_ISDN_CAUSE_RESP_STATUS_INQ:
		return "response to status enquiery";
	case OSMO_CC_ISDN_CAUSE_NORMAL_UNSPEC:
		return "normal, uspecified";
	case OSMO_CC_ISDN_CAUSE_NO_CIRCUIT_CHAN:
		return "no circuit/channel available";
	case OSMO_CC_ISDN_CAUSE_NETWORK_OOO:
		return "network out of order";
	case OSMO_CC_ISDN_CAUSE_TEMP_FAILURE:
		return "temporary failure";
	case OSMO_CC_ISDN_CAUSE_SWITCH_CONG:
		return "switching equipment congested";
	case OSMO_CC_ISDN_CAUSE_ACC_INF_DISCARD:
		return "access information discarded";
	case OSMO_CC_ISDN_CAUSE_REQ_CHAN_UNAVAIL:
		return "requested circuit/channel unavailable";
	case OSMO_CC_ISDN_CAUSE_RESOURCE_UNAVAIL:
		return "resource unavailable";
	case OSMO_CC_ISDN_CAUSE_QOS_UNAVAIL:
		return "quality of service unavailable";
	case OSMO_CC_ISDN_CAUSE_REQ_FAC_NOT_SUBSC:
		return "requested facility not subscribed";
	case OSMO_CC_ISDN_CAUSE_INC_BARRED_CUG:
		return "inc barred in closed user group";
	case OSMO_CC_ISDN_CAUSE_BEARER_CAP_UNAUTH:
		return "bearer capability unauthorized";
	case OSMO_CC_ISDN_CAUSE_BEARER_CA_UNAVAIL:
		return "bearer capability not available";
	case OSMO_CC_ISDN_CAUSE_SERV_OPT_UNAVAIL:
		return "service or option not available";
	case OSMO_CC_ISDN_CAUSE_BEARERSERV_UNIMPL:
		return "bearer service unimplemented";
	case OSMO_CC_ISDN_CAUSE_ACM_GE_ACM_MAX:
		return "acm ge ach max";
	case OSMO_CC_ISDN_CAUSE_REQ_FAC_NOTIMPL:
		return "requrested facility not implemented";
	case OSMO_CC_ISDN_CAUSE_RESTR_BCAP_AVAIL:
		return "restricted bearer capabilitey available";
	case OSMO_CC_ISDN_CAUSE_SERV_OPT_UNIMPL:
		return "service or option unimplemented";
	case OSMO_CC_ISDN_CAUSE_INVAL_CALLREF:
		return "invalid call reference";
	case OSMO_CC_ISDN_CAUSE_USER_NOT_IN_CUG:
		return "user not in closed user group";
	case OSMO_CC_ISDN_CAUSE_INCOMPAT_DEST:
		return "incompatible destination";
	case OSMO_CC_ISDN_CAUSE_INVAL_TRANS_NET:
		return "invalid transit network";
	case OSMO_CC_ISDN_CAUSE_SEMANTIC_INCORR:
		return "semantically incorrect";
	case OSMO_CC_ISDN_CAUSE_INVAL_MAND_INF:
		return "invalid mandatory information";
	case OSMO_CC_ISDN_CAUSE_MSGTYPE_NOTEXIST:
		return "message type does not exist";
	case OSMO_CC_ISDN_CAUSE_MSGTYPE_INCOMPAT:
		return "message type incompatible";
	case OSMO_CC_ISDN_CAUSE_IE_NOTEXIST:
		return "informaton element does not exits";
	case OSMO_CC_ISDN_CAUSE_COND_IE_ERR:
		return "conditional information element error";
	case OSMO_CC_ISDN_CAUSE_MSG_INCOMP_STATE:
		return "message at incompatlible state";
	case OSMO_CC_ISDN_CAUSE_RECOVERY_TIMER:
		return "recovery on time expiery";
	case OSMO_CC_ISDN_CAUSE_PROTO_ERR:
		return "protocol error";
	case OSMO_CC_ISDN_CAUSE_INTERWORKING:
		return "interworking, unspecified";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_location_name(uint8_t location)
{
	switch (location) {
	case OSMO_CC_LOCATION_USER:
		return "user";
	case OSMO_CC_LOCATION_PRIV_SERV_LOC_USER:
		return "private network serving local user";
	case OSMO_CC_LOCATION_PUB_SERV_LOC_USER:
		return "public network serving local user";
	case OSMO_CC_LOCATION_TRANSIT:
		return "transit network";
	case OSMO_CC_LOCATION_PUB_SERV_REM_USER:
		return "public network serving remote user";
	case OSMO_CC_LOCATION_PRIV_SERV_REM_USER:
		return "private network serving remote user";
	case OSMO_CC_LOCATION_BEYOND_INTERWORKING:
		return "beyond interworking";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_progress_name(uint8_t progress)
{
	switch (progress) {
	case OSMO_CC_PROGRESS_NOT_END_TO_END_ISDN:
		return "not end-to-end ISDN";
	case OSMO_CC_PROGRESS_DEST_NOT_ISDN:
		return "destination not ISDN";
	case OSMO_CC_PROGRESS_ORIG_NOT_ISDN:
		return "originator not ISDN";
	case OSMO_CC_PROGRESS_RETURN_TO_ISDN:
		return "return to ISDN";
	case OSMO_CC_PROGRESS_INTERWORKING:
		return "interworking";
	case OSMO_CC_PROGRESS_INBAND_INFO_AVAILABLE:
		return "inmand information available (audio)";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_bearer_capability_name(uint8_t capability)
{
	switch (capability) {
	case OSMO_CC_CAPABILITY_SPEECH:
		return "speech";
	case OSMO_CC_CAPABILITY_DATA:
		return "data";
	case OSMO_CC_CAPABILITY_DATA_RESTRICTED:
		return "data restricted";
	case OSMO_CC_CAPABILITY_AUDIO:
		return "audio";
	case OSMO_CC_CAPABILITY_DATA_WITH_TONES:
		return "data with tones";
	case OSMO_CC_CAPABILITY_VIDEO:
		return "video";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_bearer_mode_name(uint8_t mode)
{
	switch (mode) {
	case OSMO_CC_MODE_CIRCUIT:
		return "circuit";
	case OSMO_CC_MODE_PACKET:
		return "packet";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_dtmf_mode_name(uint8_t mode)
{
	switch (mode) {
	case OSMO_CC_DTMF_MODE_OFF:
		return "off";
	case OSMO_CC_DTMF_MODE_ON:
		return "on";
	case OSMO_CC_DTMF_MODE_DIGITS:
		return "digit";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_socket_cause_name(uint8_t cause)
{
	switch (cause) {
	case 0:
		return "unset";
	case OSMO_CC_SOCKET_CAUSE_VERSION_MISMATCH:
		return "version mismatch";
	case OSMO_CC_SOCKET_CAUSE_FAILED:
		return "socket failed";
	case OSMO_CC_SOCKET_CAUSE_BROKEN_PIPE:
		return "broken pipe";
	case OSMO_CC_SOCKET_CAUSE_TIMEOUT:
		return "keepalive timeout";
	default:
		return "<unknown>";
	}
}

const char *osmo_cc_network_type_name(uint8_t type)
{
	switch (type) {
	case OSMO_CC_NETWORK_UNDEFINED:
		return "";
	case OSMO_CC_NETWORK_ALSA_NONE:
		return "alsa";
	case OSMO_CC_NETWORK_POTS_NONE:
		return "pots";
	case OSMO_CC_NETWORK_ISDN_NONE:
		return "isdn";
	case OSMO_CC_NETWORK_SIP_NONE:
		return "sip";
	case OSMO_CC_NETWORK_GSM_IMSI:
		return "gsm-imsi";
	case OSMO_CC_NETWORK_GSM_IMEI:
		return "gsm-imei";
	case OSMO_CC_NETWORK_WEB_NONE:
		return "web";
	case OSMO_CC_NETWORK_DECT_NONE:
		return "decs";
	case OSMO_CC_NETWORK_BLUETOOTH_NONE:
		return "bluetooth";
	case OSMO_CC_NETWORK_SS5_NONE:
		return "ss5";
	case OSMO_CC_NETWORK_ANETZ_NONE:
		return "anetz";
	case OSMO_CC_NETWORK_BNETZ_MUENZ:
		return "bnetz";
	case OSMO_CC_NETWORK_CNETZ_NONE:
		return "cnetz";
	case OSMO_CC_NETWORK_NMT_NONE:
		return "nmt";
	case OSMO_CC_NETWORK_R2000_NONE:
		return "radiocom2000";
	case OSMO_CC_NETWORK_AMPS_ESN:
		return "amps";
	case OSMO_CC_NETWORK_MTS_NONE:
		return "mts";
	case OSMO_CC_NETWORK_IMTS_NONE:
		return "imts";
	case OSMO_CC_NETWORK_EUROSIGNAL_NONE:
		return "eurosignal";
	case OSMO_CC_NETWORK_JOLLYCOM_NONE:
		return "jollycom";
	case OSMO_CC_NETWORK_MPT1327_PSTN:
		return "mpt1327-pstn";
	case OSMO_CC_NETWORK_MPT1327_PBX:
		return "mpt1327-pbx";
	default:
		return "<unknown>";
	}
}

/* create message with maximum size */
osmo_cc_msg_t *osmo_cc_new_msg(uint8_t msg_type)
{
	osmo_cc_msg_t *msg;

	/* allocate message */
	msg = calloc(1, sizeof(*msg) + 65535);
	if (!msg) {
		PDEBUG(DCC, DEBUG_ERROR, "No memory\n");
		abort();
	}
	/* set message type and zero length */
	msg->type = msg_type;
	msg->length_networkorder = htons(0);

	return msg;
}

/* clone message */
osmo_cc_msg_t *osmo_cc_clone_msg(osmo_cc_msg_t *msg)
{
	osmo_cc_msg_t *new_msg;

	new_msg = osmo_cc_new_msg(msg->type);
	new_msg->length_networkorder = msg->length_networkorder;
	memcpy(new_msg->data, msg->data, ntohs(msg->length_networkorder));

	return new_msg;
}

osmo_cc_msg_t *osmo_cc_msg_list_dequeue(osmo_cc_msg_list_t **mlp, uint32_t *callref_p)
{
	osmo_cc_msg_list_t *ml;
	osmo_cc_msg_t *msg;

	ml = *mlp;
	msg = ml->msg;
	if (callref_p)
		*callref_p = ml->callref;
	*mlp = ml->next;
	free(ml);

	return msg;
}

osmo_cc_msg_list_t *osmo_cc_msg_list_enqueue(osmo_cc_msg_list_t **mlp, osmo_cc_msg_t *msg, uint32_t callref)
{
	osmo_cc_msg_list_t *ml;

	ml = calloc(1, sizeof(*ml));
	ml->msg = msg;
	ml->callref = callref;
	while (*mlp)
		mlp = &((*mlp)->next);
	*mlp = ml;

	return ml;
}

/* destroy message */
void osmo_cc_free_msg(osmo_cc_msg_t *msg)
{
	free(msg);
}

void osmo_cc_debug_ie(osmo_cc_msg_t *msg, int level)
{
	uint16_t msg_len, len;
	uint8_t *p;
	osmo_cc_ie_t *ie;
	int rc;
	int ie_repeat[256];
	uint8_t type, plan, present, screen, coding, capability, mode, progress, reason, duration_ms, pause_ms, dtmf_mode, location, notify, isdn_cause, socket_cause;
	uint16_t sip_cause;
	uint32_t unique;
	char string[65536];
	int i;

	memset(ie_repeat, 0, sizeof(ie_repeat));

	msg_len = ntohs(msg->length_networkorder);
	p = msg->data;

	while (msg_len) {
		ie = (osmo_cc_ie_t *)p;
		/* check for minimum IE length */
		if (msg_len < sizeof(*ie)) {
			PDEBUG(DCC, level, "****** Rest of message is too short for an IE: value=%s\n", debug_hex(p, msg_len));
			return;
		}
		/* get actual IE length */
		len = ntohs(ie->length_networkorder);
		/* check if IE length does not exceed message */
		if (msg_len < sizeof(*ie) + len) {
			PDEBUG(DCC, level, "****** IE: type=0x%02x length=%d would exceed the rest length of message (%d bytes left)\n", ie->type, len, msg_len - (int)sizeof(*ie));
			return;
		}
		switch (ie->type) {
		case OSMO_CC_IE_CALLED:
			rc = osmo_cc_get_ie_called(msg, ie_repeat[ie->type], &type, &plan, string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s type=%d(%s) plan=%d(%s) number='%s'\n", osmo_cc_ie_name(ie->type), type, osmo_cc_number_type_name(type), plan, osmo_cc_number_plan_name(plan), string);
			break;
		case OSMO_CC_IE_CALLED_SUB:
			rc = osmo_cc_get_ie_called_sub(msg, ie_repeat[ie->type], &type, string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s type=%d(%s) number='%s'\n", osmo_cc_ie_name(ie->type), type, osmo_cc_number_type_name(type), string);
			break;
		case OSMO_CC_IE_CALLED_NAME:
			rc = osmo_cc_get_ie_called_name(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s name='%s'\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_CALLED_INTERFACE:
			rc = osmo_cc_get_ie_called_interface(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s name='%s'\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_COMPLETE:
			rc = osmo_cc_get_ie_complete(msg, ie_repeat[ie->type]);
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s\n", osmo_cc_ie_name(ie->type));
			break;
		case OSMO_CC_IE_CALLING:
			rc = osmo_cc_get_ie_calling(msg, ie_repeat[ie->type], &type, &plan, &present, &screen, string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s type=%d(%s) plan=%d(%s), presentation=%d(%s), screening=%d(%s), number='%s'\n", osmo_cc_ie_name(ie->type), type, osmo_cc_number_type_name(type), plan, osmo_cc_number_plan_name(plan), present, osmo_cc_number_present_name(present), screen, osmo_cc_number_screen_name(screen), string);
			break;
		case OSMO_CC_IE_CALLING_SUB:
			rc = osmo_cc_get_ie_calling_sub(msg, ie_repeat[ie->type], &type, string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s type=%d(%s) number='%s'\n", osmo_cc_ie_name(ie->type), type, osmo_cc_number_type_name(type), string);
			break;
		case OSMO_CC_IE_CALLING_NAME:
			rc = osmo_cc_get_ie_calling_name(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s name='%s'\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_CALLING_INTERFACE:
			rc = osmo_cc_get_ie_calling_interface(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s name='%s'\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_CALLING_NETWORK:
			rc = osmo_cc_get_ie_calling_network(msg, ie_repeat[ie->type], &type, string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s type=%d(%s) id='%s'\n", osmo_cc_ie_name(ie->type), type, osmo_cc_network_type_name(type), string);
			break;
		case OSMO_CC_IE_BEARER:
			rc = osmo_cc_get_ie_bearer(msg, ie_repeat[ie->type], &coding, &capability, &mode);
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s coding=%d(%s) capability=%d(%s) mode=%d(%s)\n", osmo_cc_ie_name(ie->type), coding, osmo_cc_coding_name(coding), capability, osmo_cc_bearer_capability_name(capability), mode, osmo_cc_bearer_mode_name(mode));
			break;
		case OSMO_CC_IE_REDIR:
			rc = osmo_cc_get_ie_redir(msg, ie_repeat[ie->type], &type, &plan, &present, &screen, &reason, string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s type=%d(%s) plan=%d(%s) presentation=%d(%s) screening=%d(%s) reason=%d(%s) number='%s'\n", osmo_cc_ie_name(ie->type), type, osmo_cc_number_type_name(type), plan, osmo_cc_number_plan_name(plan), present, osmo_cc_number_present_name(present), screen, osmo_cc_number_screen_name(screen), reason, osmo_cc_redir_reason_name(reason), string);
			break;
		case OSMO_CC_IE_DTMF:
			rc = osmo_cc_get_ie_dtmf(msg, ie_repeat[ie->type], &duration_ms, &pause_ms, &dtmf_mode, string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s duration=%dms pause=%dms mode=%d(%s)\n", osmo_cc_ie_name(ie->type), duration_ms, pause_ms, dtmf_mode, osmo_cc_dtmf_mode_name(dtmf_mode));
			break;
		case OSMO_CC_IE_KEYPAD:
			rc = osmo_cc_get_ie_keypad(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s digits='%s'\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_PROGRESS:
			rc = osmo_cc_get_ie_progress(msg, ie_repeat[ie->type], &coding, &location, &progress);
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s coding=%d(%s) location=%d(%s) progress=%d(%s)\n", osmo_cc_ie_name(ie->type), coding, osmo_cc_coding_name(coding), location, osmo_cc_location_name(location), progress, osmo_cc_progress_name(progress));
			break;
		case OSMO_CC_IE_NOTIFY:
			rc = osmo_cc_get_ie_notify(msg, ie_repeat[ie->type], &notify);
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s indicator=%d(%s)\n", osmo_cc_ie_name(ie->type), notify, osmo_cc_notify_name(notify));
			break;
		case OSMO_CC_IE_CAUSE:
			rc = osmo_cc_get_ie_cause(msg, ie_repeat[ie->type], &location, &isdn_cause, &sip_cause, &socket_cause);
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s location=%d(%s) isdn_cause=%d(%s) sip_cause=%d socket_cause=%d(%s)\n", osmo_cc_ie_name(ie->type), location, osmo_cc_location_name(location), isdn_cause, osmo_cc_isdn_cause_name(isdn_cause), sip_cause, socket_cause, osmo_cc_socket_cause_name(socket_cause));
			break;
		case OSMO_CC_IE_DISPLAY:
			rc = osmo_cc_get_ie_display(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s info='%s'\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_SDP:
			rc = osmo_cc_get_ie_sdp(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			for (i = 0; string[i]; i++) {
				if (string[i] == '\r')
					string[i] = '\\';
				if (string[i] == '\n')
					string[i] = 'n';
			}
			PDEBUG(DCC, level, "  %s payload=%s\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_SOCKET_ADDRESS:
			rc = osmo_cc_get_ie_socket_address(msg, ie_repeat[ie->type], string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s address='%s'\n", osmo_cc_ie_name(ie->type), string);
			break;
		case OSMO_CC_IE_PRIVATE:
			rc = osmo_cc_get_ie_private(msg, ie_repeat[ie->type], &unique, (uint8_t *)string, sizeof(string));
			if (rc < 0)
				break;
			PDEBUG(DCC, level, "  %s unique=%u=0x%08x private=%s\n", osmo_cc_ie_name(ie->type), unique, unique, debug_hex((uint8_t *)string, rc));
			break;
		default:
			PDEBUG(DCC, level, "  %s type=0x%02x length=%d value=%s\n", osmo_cc_ie_name(ie->type), ie->type, len, debug_hex(ie->data, len));
		}
		ie_repeat[ie->type]++;
		p += sizeof(*ie) + len;
		msg_len -= sizeof(*ie) + len;
	}
}

/* search and return information element
 * we give the IE type we are searching for
 * we also give the repetition, to find IE that is repeated
 * the result is stored in *ie_data
 * the return length is the length that exceeds the given ie_len
 * if there is an error, a value < 0 is returned
 */
int osmo_cc_get_ie_struct(osmo_cc_msg_t *msg, uint8_t ie_type, int ie_repeat, int ie_len, const osmo_cc_ie_t **ie_struct)
{
	uint16_t msg_len, len;
	uint8_t *p;
	osmo_cc_ie_t *ie;

	msg_len = ntohs(msg->length_networkorder);
	p = msg->data;

	while (msg_len) {
		ie = (osmo_cc_ie_t *)p;
		/* check for minimum IE length */
		if (msg_len < sizeof(*ie)) {
			PDEBUG(DCC, DEBUG_ERROR, "MSG short read\n");
			osmo_cc_debug_ie(msg, DEBUG_ERROR);
			return -EINVAL;
		}
		/* get actual IE length */
		len = ntohs(ie->length_networkorder);
		/* check if IE length does not exceed message */
		if (msg_len < sizeof(*ie) + len) {
			PDEBUG(DCC, DEBUG_ERROR, "MSG short read\n");
			osmo_cc_debug_ie(msg, DEBUG_ERROR);
			return -EINVAL;
		}
		/* check if IE matches the one that is searched for */
		if (ie->type != ie_type) {
			p += sizeof(*ie) + len;
			msg_len -= sizeof(*ie) + len;
			continue;
		}
		/* check if IE repetition exists */
		if (ie_repeat) {
			--ie_repeat;
			p += sizeof(*ie) + len;
			msg_len -= sizeof(*ie) + len;
			continue;
		}
		/* return IE and indicate how many bytes we have more than the given length*/
		if (ntohs(ie->length_networkorder) < ie_len) {
			PDEBUG(DCC, DEBUG_ERROR, "IE 0x%02d has length of %d, but we expect it to have at least %d!\n", ie_type, ntohs(ie->length_networkorder), ie_len);
			return -EINVAL;
		}
		*ie_struct = ie;
		return ntohs(ie->length_networkorder) - ie_len;
	}

	/* IE not found */
	return -EINVAL;
}

/* as above, but return data of IE only */
int osmo_cc_get_ie_data(osmo_cc_msg_t *msg, uint8_t ie_type, int ie_repeat, int ie_len, const void **ie_data)
{
	const osmo_cc_ie_t *ie;
	int rc;

	rc = osmo_cc_get_ie_struct(msg, ie_type, ie_repeat, ie_len, &ie);
	if (rc >= 0)
		*ie_data = ie->data;

	return rc;
}

/* as above, but return 1 if IE exists */
int osmo_cc_has_ie(osmo_cc_msg_t *msg, uint8_t ie_type, int ie_repeat)
{
	const osmo_cc_ie_t *ie;
	int rc;

	rc = osmo_cc_get_ie_struct(msg, ie_type, ie_repeat, 0, &ie);
	if (rc >= 0)
		return 1;

	return 0;
}

/* remove IE from message */
int osmo_cc_remove_ie(osmo_cc_msg_t *msg, uint8_t ie_type, int ie_repeat)
{
	const osmo_cc_ie_t *ie;
	int rc;
	int msg_len, before_ie, ie_size, after_ie;

	rc = osmo_cc_get_ie_struct(msg, ie_type, ie_repeat, 0, &ie);
	if (rc < 0)
		return rc;

	msg_len = ntohs(msg->length_networkorder);
	before_ie = (void *)ie - (void *)msg->data;
	ie_size = sizeof(*ie) + ntohs(ie->length_networkorder);
	after_ie = msg_len - ie_size - before_ie;
	if (after_ie)
		memcpy(msg->data + before_ie, msg->data + before_ie + ie_size, after_ie);
	msg->length_networkorder = htons(msg_len - ie_size);

	return 0;
}

/* add information element
 * the type is given by ie_type and length is given by ie_len
 * the return value is a pointer to the data of the IE
 */
void *osmo_cc_add_ie(osmo_cc_msg_t *msg, uint8_t ie_type, int ie_len)
{
	uint16_t msg_len;
	int new_msg_len;
	uint8_t *p;
	osmo_cc_ie_t *ie;

	/* get pointer to first IE, if any */
	p = msg->data;
	/* expand messasge */
	msg_len = ntohs(msg->length_networkorder);
	new_msg_len = msg_len + sizeof(*ie) + ie_len;
	if (new_msg_len > 65535) {
		PDEBUG(DCC, DEBUG_ERROR, "MSG overflow\n");
		return NULL;
	}
	msg->length_networkorder = htons(new_msg_len);
	/* go to end of (unexpanded) message */
	ie = (osmo_cc_ie_t *)(p + msg_len);
	/* add ie */
	ie->type = ie_type;
	ie->length_networkorder = htons(ie_len);
	memset(ie->data, 0, ie_len); /* just in case there is something, but it shouldn't */

	return ie->data;
}

/* gets the information element's data that *iep points to and returns that ie.
 * if *iep points to msg->data, the first IE's data is returned. (must be set before first call.)
 * if *iep points to the end of the message, NULL is returned.
 * if there is no next IE, *iep is set to point to the end of message.
 */
void *osmo_cc_msg_sep_ie(osmo_cc_msg_t *msg, void **iep, uint8_t *ie_type, uint16_t *ie_length)
{
	uint16_t msg_len;
	osmo_cc_ie_t *ie;

	/* in case that *iep points to start of message, make it point to first IE */
	if (*iep == msg)
		*iep = msg->data;
	/* case IE */
	ie = *iep;
	/* check if it is NULL */
	if (ie == NULL)
		return NULL;
	/* check if it points to the end of message or there is not at least an IE header */
	msg_len = ntohs(msg->length_networkorder);
	if ((int)((uint8_t *)ie - msg->data) > (int)(msg_len - sizeof(*ie)))
		return NULL;
	/* increment iep and return IE */
	*ie_type = ie->type;
	*ie_length = ntohs(ie->length_networkorder);
	*iep = (uint8_t *)ie + sizeof(*ie) + *ie_length;
	return ie->data;
}

/* copy given block to given string with given size */
static void _ie2string(char *string, size_t string_size, const char *ie_string, int ie_size)
{
	int copy_size;

	copy_size = string_size - 1;
	if (ie_size < copy_size)
		copy_size = ie_size;
	memcpy(string, ie_string, copy_size);
	string[copy_size] = '\0';
}

/* helper to encode called party number (dialing) */
void osmo_cc_add_ie_called(osmo_cc_msg_t *msg, uint8_t type, uint8_t plan, const char *dialing)
{
	struct osmo_cc_ie_called *ie_called;

	ie_called = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLED, sizeof(*ie_called) + strlen(dialing));
	ie_called->type = type;
	ie_called->plan = plan;
	memcpy(ie_called->digits, dialing, strlen(dialing));
}

/* helper to decode called party number (dialing) */
int osmo_cc_get_ie_called(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *type, uint8_t *plan, char *dialing, size_t dialing_size)
{
	struct osmo_cc_ie_called *ie_called;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLED, ie_repeat, sizeof(*ie_called), (const void **)&ie_called);
	if (rc < 0)
		return rc;
	*type = ie_called->type;
	*plan = ie_called->plan;
	_ie2string(dialing, dialing_size, ie_called->digits, rc);
	return rc;
}

/* helper to encode called party sub address (dialing) */
void osmo_cc_add_ie_called_sub(osmo_cc_msg_t *msg, uint8_t type, const char *dialing)
{
	struct osmo_cc_ie_called_sub *ie_called_sub;

	ie_called_sub = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLED_SUB, sizeof(*ie_called_sub) + strlen(dialing));
	ie_called_sub->type = type;
	memcpy(ie_called_sub->digits, dialing, strlen(dialing));
}

/* helper to decode called party sub address (dialing) */
int osmo_cc_get_ie_called_sub(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *type, char *dialing, size_t dialing_size)
{
	struct osmo_cc_ie_called_sub *ie_called_sub;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLED_SUB, ie_repeat, sizeof(*ie_called_sub), (const void **)&ie_called_sub);
	if (rc < 0)
		return rc;
	*type = ie_called_sub->type;
	_ie2string(dialing, dialing_size, ie_called_sub->digits, rc);
	return rc;
}

/* helper to encode called party name (dialing) */
void osmo_cc_add_ie_called_name(osmo_cc_msg_t *msg, const char *name)
{
	struct osmo_cc_ie_called_name *ie_called_name;

	ie_called_name = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLED_NAME, sizeof(*ie_called_name) + strlen(name));
	memcpy(ie_called_name->name, name, strlen(name));
}

/* helper to decode called party name (dialing) */
int osmo_cc_get_ie_called_name(osmo_cc_msg_t *msg, int ie_repeat, char *name, size_t name_size)
{
	struct osmo_cc_ie_called_name *ie_called_name;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLED_NAME, ie_repeat, sizeof(*ie_called_name), (const void **)&ie_called_name);
	if (rc < 0)
		return rc;
	_ie2string(name, name_size, ie_called_name->name, rc);
	return rc;
}

/* helper to encode called interface name */
void osmo_cc_add_ie_called_interface(osmo_cc_msg_t *msg, const char *interface)
{
	struct osmo_cc_ie_called_interface *ie_interface;

	ie_interface = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLED_INTERFACE, sizeof(*ie_interface) + strlen(interface));
	memcpy(ie_interface->name, interface, strlen(interface));
}

/* helper to decode called interface name */
int osmo_cc_get_ie_called_interface(osmo_cc_msg_t *msg, int ie_repeat, char *interface, size_t interface_size)
{
	struct osmo_cc_ie_called_interface *ie_interface;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLED_INTERFACE, ie_repeat, sizeof(*ie_interface), (const void **)&ie_interface);
	if (rc < 0)
		return rc;
	_ie2string(interface, interface_size, ie_interface->name, rc);
	return rc;
}

/* helper to encode complete IE */
void osmo_cc_add_ie_complete(osmo_cc_msg_t *msg)
{
	osmo_cc_add_ie(msg, OSMO_CC_IE_COMPLETE, 0);
}

/* helper to decode complete IE */
int osmo_cc_get_ie_complete(osmo_cc_msg_t *msg, int ie_repeat)
{
	int rc;
	void *ie_complete;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_COMPLETE, ie_repeat, 0, (const void **)&ie_complete);
	return rc;
}

/* helper to encode calling/connected party number (caller ID or connected ID) */
void osmo_cc_add_ie_calling(osmo_cc_msg_t *msg, uint8_t type, uint8_t plan, uint8_t present, uint8_t screen, const char *callerid)
{
	struct osmo_cc_ie_calling *ie_calling;

	ie_calling = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLING, sizeof(*ie_calling) + strlen(callerid));
	ie_calling->type = type;
	ie_calling->plan = plan;
	ie_calling->present = present;
	ie_calling->screen = screen;
	memcpy(ie_calling->digits, callerid, strlen(callerid));
}

/* helper to decode calling/connected party number (caller ID or connected ID) */
int osmo_cc_get_ie_calling(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *type, uint8_t *plan, uint8_t *present, uint8_t *screen, char *callerid, size_t callerid_size)
{
	struct osmo_cc_ie_calling *ie_calling;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLING, ie_repeat, sizeof(*ie_calling), (const void **)&ie_calling);
	if (rc < 0)
		return rc;
	*type = ie_calling->type;
	*plan = ie_calling->plan;
	*present = ie_calling->present;
	*screen = ie_calling->screen;
	_ie2string(callerid, callerid_size, ie_calling->digits, rc);
	return rc;
}

/* helper to encode calling/connected sub address (caller ID or connected ID) */
void osmo_cc_add_ie_calling_sub(osmo_cc_msg_t *msg, uint8_t type, const char *callerid)
{
	struct osmo_cc_ie_calling_sub *ie_calling_sub;

	ie_calling_sub = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLING_SUB, sizeof(*ie_calling_sub) + strlen(callerid));
	ie_calling_sub->type = type;
	memcpy(ie_calling_sub->digits, callerid, strlen(callerid));
}

/* helper to decode calling/connected sub address (caller ID or connected ID) */
int osmo_cc_get_ie_calling_sub(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *type, char *callerid, size_t callerid_size)
{
	struct osmo_cc_ie_calling_sub *ie_calling_sub;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLING_SUB, ie_repeat, sizeof(*ie_calling_sub), (const void **)&ie_calling_sub);
	if (rc < 0)
		return rc;
	*type = ie_calling_sub->type;
	_ie2string(callerid, callerid_size, ie_calling_sub->digits, rc);
	return rc;
}

/* helper to encode calling/connected name (caller ID or connected ID) */
void osmo_cc_add_ie_calling_name(osmo_cc_msg_t *msg, const char *name)
{
	struct osmo_cc_ie_calling_name *ie_calling_name;

	ie_calling_name = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLING_NAME, sizeof(*ie_calling_name) + strlen(name));
	memcpy(ie_calling_name->name, name, strlen(name));
}

/* helper to decode calling/connected name address (caller ID or connected ID) */
int osmo_cc_get_ie_calling_name(osmo_cc_msg_t *msg, int ie_repeat, char *name, size_t name_size)
{
	struct osmo_cc_ie_calling_name *ie_calling_name;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLING_NAME, ie_repeat, sizeof(*ie_calling_name), (const void **)&ie_calling_name);
	if (rc < 0)
		return rc;
	_ie2string(name, name_size, ie_calling_name->name, rc);
	return rc;
}

/* helper to encode calling interface name */
void osmo_cc_add_ie_calling_interface(osmo_cc_msg_t *msg, const char *interface)
{
	struct osmo_cc_ie_calling_interface *ie_interface;

	ie_interface = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLING_INTERFACE, sizeof(*ie_interface) + strlen(interface));
	memcpy(ie_interface->name, interface, strlen(interface));
}

/* helper to decode calling interface name */
int osmo_cc_get_ie_calling_interface(osmo_cc_msg_t *msg, int ie_repeat, char *interface, size_t interface_size)
{
	struct osmo_cc_ie_calling_interface *ie_interface;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLING_INTERFACE, ie_repeat, sizeof(*ie_interface), (const void **)&ie_interface);
	if (rc < 0)
		return rc;
	_ie2string(interface, interface_size, ie_interface->name, rc);
	return rc;
}

/* helper to encode network specific caller/connected ID */
void osmo_cc_add_ie_calling_network(osmo_cc_msg_t *msg, uint8_t type, const char *networkid)
{
	struct osmo_cc_ie_network *ie_network;

	ie_network = osmo_cc_add_ie(msg, OSMO_CC_IE_CALLING_NETWORK, sizeof(*ie_network) + strlen(networkid));
	ie_network->type = type;
	memcpy(ie_network->id, networkid, strlen(networkid));
}

/* helper to encode network specific caller/connected ID */
int osmo_cc_get_ie_calling_network(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *type, char *networkid, size_t networkid_size)
{
	struct osmo_cc_ie_network *ie_network;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CALLING_NETWORK, ie_repeat, sizeof(*ie_network), (const void **)&ie_network);
	if (rc < 0)
		return rc;
	*type = ie_network->type;
	_ie2string(networkid, networkid_size, ie_network->id, rc);
	return rc;
}

/* helper to encode bearer capability */
void osmo_cc_add_ie_bearer(osmo_cc_msg_t *msg, uint8_t coding, uint8_t capability, uint8_t mode)
{
	struct osmo_cc_ie_bearer *ie_bearer;

	ie_bearer = osmo_cc_add_ie(msg, OSMO_CC_IE_BEARER, sizeof(*ie_bearer));
	ie_bearer->coding = coding;
	ie_bearer->capability = capability;
	ie_bearer->mode = mode;
}

/* helper to decode bearer capability */
int osmo_cc_get_ie_bearer(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *coding, uint8_t *capability, uint8_t *mode)
{
	struct osmo_cc_ie_bearer *ie_bearer;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_BEARER, ie_repeat, sizeof(*ie_bearer), (const void **)&ie_bearer);
	if (rc < 0)
		return rc;
	*coding = ie_bearer->coding;
	*capability = ie_bearer->capability;
	*mode = ie_bearer->mode;
	return rc;
}

/* helper to encode redirection and redirecting number */
void osmo_cc_add_ie_redir(osmo_cc_msg_t *msg, uint8_t type, uint8_t plan, uint8_t present, uint8_t screen, uint8_t redir_reason, const char *callerid)
{
	struct osmo_cc_ie_redir *ie_redir;

	ie_redir = osmo_cc_add_ie(msg, OSMO_CC_IE_REDIR, sizeof(*ie_redir) + strlen(callerid));
	ie_redir->type = type;
	ie_redir->plan = plan;
	ie_redir->present = present;
	ie_redir->screen = screen;
	ie_redir->redir_reason = redir_reason;
	memcpy(ie_redir->digits, callerid, strlen(callerid));
}

/* helper to decode redirection and redirecting number */
int osmo_cc_get_ie_redir(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *type, uint8_t *plan, uint8_t *present, uint8_t *screen, uint8_t *reason, char *callerid, size_t callerid_size)
{
	struct osmo_cc_ie_redir *ie_redir;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_REDIR, ie_repeat, sizeof(*ie_redir), (const void **)&ie_redir);
	if (rc < 0)
		return rc;
	*type = ie_redir->type;
	*plan = ie_redir->plan;
	*present = ie_redir->present;
	*screen = ie_redir->screen;
	*reason = ie_redir->redir_reason;
	_ie2string(callerid, callerid_size, ie_redir->digits, rc);
	return rc;
}

/* helper to encode DTMF tones */
void osmo_cc_add_ie_dtmf(osmo_cc_msg_t *msg, uint8_t duration_ms, uint8_t pause_ms, uint8_t dtmf_mode, const char *digits)
{
	struct osmo_cc_ie_dtmf *ie_dtmf;

	ie_dtmf = osmo_cc_add_ie(msg, OSMO_CC_IE_DTMF, sizeof(*ie_dtmf) + strlen(digits));
	ie_dtmf->duration_ms = duration_ms;
	ie_dtmf->pause_ms = pause_ms;
	ie_dtmf->dtmf_mode = dtmf_mode;
	memcpy(ie_dtmf->digits, digits, strlen(digits));
}

/* helper to decode DTMF tones */
int osmo_cc_get_ie_dtmf(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *duration_ms, uint8_t *pause_ms, uint8_t *dtmf_mode, char *digits, size_t digits_size)
{
	struct osmo_cc_ie_dtmf *ie_dtmf;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_DTMF, ie_repeat, sizeof(*ie_dtmf), (const void **)&ie_dtmf);
	if (rc < 0)
		return rc;
	*duration_ms = ie_dtmf->duration_ms;
	*pause_ms = ie_dtmf->pause_ms;
	*dtmf_mode = ie_dtmf->dtmf_mode;
	_ie2string(digits, digits_size, ie_dtmf->digits, rc);
	return rc;
}

/* helper to encode keypad press */
void osmo_cc_add_ie_keypad(osmo_cc_msg_t *msg, const char *digits)
{
	struct osmo_cc_ie_keypad *ie_keypad;

	ie_keypad = osmo_cc_add_ie(msg, OSMO_CC_IE_KEYPAD, sizeof(*ie_keypad) + strlen(digits));
	memcpy(ie_keypad->digits, digits, strlen(digits));
}

/* helper to decode keypad press */
int osmo_cc_get_ie_keypad(osmo_cc_msg_t *msg, int ie_repeat, char *digits, size_t digits_size)
{
	struct osmo_cc_ie_keypad *ie_keypad;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_KEYPAD, ie_repeat, sizeof(*ie_keypad), (const void **)&ie_keypad);
	if (rc < 0)
		return rc;
	_ie2string(digits, digits_size, ie_keypad->digits, rc);
	return rc;
}

/* helper to encode call progress information */
void osmo_cc_add_ie_progress(osmo_cc_msg_t *msg, uint8_t coding, uint8_t location, uint8_t progress)
{
	struct osmo_cc_ie_progress *ie_progress;

	ie_progress = osmo_cc_add_ie(msg, OSMO_CC_IE_PROGRESS, sizeof(*ie_progress));
	ie_progress->coding = coding;
	ie_progress->location = location;
	ie_progress->progress = progress;
}

/* helper to decode call progress information */
int osmo_cc_get_ie_progress(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *coding, uint8_t *location, uint8_t *progress)
{
	struct osmo_cc_ie_progress *ie_progress;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_PROGRESS, ie_repeat, sizeof(*ie_progress), (const void **)&ie_progress);
	if (rc < 0)
		return rc;
	*coding = ie_progress->coding;
	*location = ie_progress->location;
	*progress = ie_progress->progress;
	return rc;
}

/* helper to encode notification */
void osmo_cc_add_ie_notify(osmo_cc_msg_t *msg, uint8_t notify)
{
	struct osmo_cc_ie_notify *ie_notify;

	ie_notify = osmo_cc_add_ie(msg, OSMO_CC_IE_NOTIFY, sizeof(*ie_notify));
	ie_notify->notify = notify;
}

/* helper to decode notification */
int osmo_cc_get_ie_notify(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *notify)
{
	struct osmo_cc_ie_notify *ie_notify;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_NOTIFY, ie_repeat, sizeof(*ie_notify), (const void **)&ie_notify);
	if (rc < 0)
		return rc;
	*notify = ie_notify->notify;
	return rc;
}

/* helper to encode cause */
void osmo_cc_add_ie_cause(osmo_cc_msg_t *msg, uint8_t location, uint8_t isdn_cause, uint16_t sip_cause, uint8_t socket_cause)
{
	struct osmo_cc_ie_cause *ie_cause;

	ie_cause = osmo_cc_add_ie(msg, OSMO_CC_IE_CAUSE, sizeof(*ie_cause));
	ie_cause->location = location;
	ie_cause->isdn_cause = isdn_cause;
	ie_cause->sip_cause_networkorder = htons(sip_cause);
	ie_cause->socket_cause = socket_cause;
}

/* helper to deccode cause */
int osmo_cc_get_ie_cause(osmo_cc_msg_t *msg, int ie_repeat, uint8_t *location, uint8_t *isdn_cause, uint16_t *sip_cause, uint8_t *socket_cause)
{
	struct osmo_cc_ie_cause *ie_cause;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_CAUSE, ie_repeat, sizeof(*ie_cause), (const void **)&ie_cause);
	if (rc < 0)
		return rc;
	*location = ie_cause->location;
	*isdn_cause = ie_cause->isdn_cause;
	*sip_cause = ntohs(ie_cause->sip_cause_networkorder);
	*socket_cause = ie_cause->socket_cause;
	return rc;
}

/* helper to encode DISPLAY information */
void osmo_cc_add_ie_display(osmo_cc_msg_t *msg, const char *text)
{
	struct osmo_cc_ie_display *ie_display;

	ie_display = osmo_cc_add_ie(msg, OSMO_CC_IE_DISPLAY, sizeof(*ie_display) + strlen(text));
	memcpy(ie_display->text, text, strlen(text));
}

/* helper to decode DISPLAY information */
int osmo_cc_get_ie_display(osmo_cc_msg_t *msg, int ie_repeat, char *text, size_t text_size)
{
	struct osmo_cc_ie_display *ie_display;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_DISPLAY, ie_repeat, sizeof(*ie_display), (const void **)&ie_display);
	if (rc < 0)
		return rc;
	_ie2string(text, text_size, ie_display->text, rc);
	return rc;
}

/* helper to encode SDP */
void osmo_cc_add_ie_sdp(osmo_cc_msg_t *msg, const char *sdp)
{
	struct osmo_cc_ie_sdp *ie_sdp;

	ie_sdp = osmo_cc_add_ie(msg, OSMO_CC_IE_SDP, sizeof(*ie_sdp) + strlen(sdp));
	memcpy(ie_sdp->sdp, sdp, strlen(sdp));
}

/* helper to decode SDP */
int osmo_cc_get_ie_sdp(osmo_cc_msg_t *msg, int ie_repeat, char *sdp, size_t sdp_size)
{
	struct osmo_cc_ie_sdp *ie_sdp;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_SDP, ie_repeat, sizeof(*ie_sdp), (const void **)&ie_sdp);
	if (rc < 0)
		return rc;
	_ie2string(sdp, sdp_size, ie_sdp->sdp, rc);
	return rc;
}

/* helper to encode socket address */
void osmo_cc_add_ie_socket_address(osmo_cc_msg_t *msg, const char *address)
{
	struct osmo_cc_ie_socket_address *ie_socket_address;

	ie_socket_address = osmo_cc_add_ie(msg, OSMO_CC_IE_SOCKET_ADDRESS, sizeof(*ie_socket_address) + strlen(address));
	memcpy(ie_socket_address->address, address, strlen(address));
}

/* helper to decode socket address */
int osmo_cc_get_ie_socket_address(osmo_cc_msg_t *msg, int ie_repeat, char *address, size_t address_size)
{
	struct osmo_cc_ie_socket_address *ie_socket_address;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_SOCKET_ADDRESS, ie_repeat, sizeof(*ie_socket_address), (const void **)&ie_socket_address);
	if (rc < 0)
		return rc;
	_ie2string(address, address_size, ie_socket_address->address, rc);
	return rc;
}

/* helper to encode private information element */
void osmo_cc_add_ie_private(osmo_cc_msg_t *msg, uint32_t unique, const uint8_t *data, size_t data_size)
{
	struct osmo_cc_ie_private *ie_private;

	ie_private = osmo_cc_add_ie(msg, OSMO_CC_IE_PRIVATE, sizeof(*ie_private) + data_size);
	ie_private->unique_networkorder = htonl(unique);
	memcpy(ie_private->data, data, data_size);
}

/* helper to decode private information element */
int osmo_cc_get_ie_private(osmo_cc_msg_t *msg, int ie_repeat, uint32_t *unique, uint8_t *data, size_t data_size)
{
	struct osmo_cc_ie_private *ie_private;
	int rc;

	rc = osmo_cc_get_ie_data(msg, OSMO_CC_IE_PRIVATE, ie_repeat, sizeof(*ie_private), (const void **)&ie_private);
	if (rc < 0)
		return rc;
	*unique = ntohl(ie_private->unique_networkorder);
	if (rc > (int)data_size)
		rc = data_size;
	memcpy(data, ie_private->data, rc);
	return rc;
}
