// SPDX-License-Identifier: GPL-2.0
/*
 * A small QMI client over QRTR for the bring-up shell.
 *
 *   qmitool sim                      UIM card and app status
 *   qmitool nas                      NAS serving system (registration, PLMN)
 *   qmitool signal                   NAS signal info
 *   qmitool mode                     DMS operating mode
 *   qmitool online                   DMS set operating mode to online
 *   qmitool raw <svc> <msg> [hex]    send any request, dump the response TLVs
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/qrtr.h>
#include "libqrtr.h"

struct tlv {
	uint8_t type;
	uint16_t len;
	const uint8_t *val;
};

static int find_service(int sock, unsigned int svc, uint32_t *node, uint32_t *port)
{
	struct qrtr_packet pkt;
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	char buf[256];
	int found = 0;
	int len;

	if (qrtr_new_lookup(sock, svc, 0, 0) < 0)
		return -1;
	for (;;) {
		if (qrtr_poll(sock, 2000) <= 0)
			break;
		len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&sq, &sl);
		if (len < 0 || qrtr_decode(&pkt, buf, len, &sq) < 0)
			break;
		if (pkt.type != QRTR_TYPE_NEW_SERVER)
			continue;
		if (!pkt.service && !pkt.node && !pkt.port)
			break;
		if (pkt.service == svc && !found) {
			*node = pkt.node;
			*port = pkt.port;
			found = 1;
		}
	}
	return found ? 0 : -1;
}

static int request(unsigned int svc, uint16_t msg, const uint8_t *tlvs, uint16_t tlvlen,
		   uint8_t *resp, int resplen)
{
	static uint16_t txn = 1;
	uint8_t req[512];
	uint32_t node, port, rnode, rport;
	int sock, len;

	sock = qrtr_open(0);
	if (sock < 0) {
		fprintf(stderr, "qrtr_open: %s\n", strerror(errno));
		return -1;
	}
	if (find_service(sock, svc, &node, &port)) {
		fprintf(stderr, "service %u not found\n", svc);
		close(sock);
		return -1;
	}
	req[0] = 0;
	req[1] = txn & 0xff;
	req[2] = txn >> 8;
	req[3] = msg & 0xff;
	req[4] = msg >> 8;
	req[5] = tlvlen & 0xff;
	req[6] = tlvlen >> 8;
	memcpy(req + 7, tlvs, tlvlen);
	if (qrtr_sendto(sock, node, port, req, 7 + tlvlen) < 0) {
		fprintf(stderr, "send: %s\n", strerror(errno));
		close(sock);
		return -1;
	}
	for (;;) {
		if (qrtr_poll(sock, 5000) <= 0) {
			fprintf(stderr, "no response\n");
			close(sock);
			return -1;
		}
		len = qrtr_recvfrom(sock, resp, resplen, &rnode, &rport);
		if (len < 7)
			continue;
		if (rnode == node && rport == port && resp[0] == 2 &&
		    (resp[1] | resp[2] << 8) == txn && (resp[3] | resp[4] << 8) == msg)
			break;
	}
	txn++;
	close(sock);
	return len;
}

static int parse(const uint8_t *resp, int len, struct tlv *tlvs, int max)
{
	int off = 7, n = 0;
	int end = 7 + (resp[5] | resp[6] << 8);

	if (end > len)
		end = len;
	while (off + 3 <= end && n < max) {
		tlvs[n].type = resp[off];
		tlvs[n].len = resp[off + 1] | resp[off + 2] << 8;
		tlvs[n].val = resp + off + 3;
		off += 3 + tlvs[n].len;
		if (off > end)
			break;
		n++;
	}
	return n;
}

static const struct tlv *get(const struct tlv *t, int n, uint8_t type)
{
	for (int i = 0; i < n; i++)
		if (t[i].type == type)
			return &t[i];
	return NULL;
}

static int result(const struct tlv *t, int n)
{
	const struct tlv *r = get(t, n, 0x02);

	if (!r || r->len < 4) {
		printf("no result TLV\n");
		return -1;
	}
	uint16_t res = r->val[0] | r->val[1] << 8, err = r->val[2] | r->val[3] << 8;
	if (res) {
		printf("QMI error 0x%04x\n", err);
		return -1;
	}
	return 0;
}

static void dump(const struct tlv *t, int n)
{
	for (int i = 0; i < n; i++) {
		printf("TLV 0x%02x (%u):", t[i].type, t[i].len);
		for (int j = 0; j < t[i].len; j++)
			printf(" %02x", t[i].val[j]);
		printf("\n");
	}
}

static const char *card_state[] = { "absent", "present", "error" };
static const char *app_state[] = { "unknown", "detected", "pin1/upin required", "puk1/upuk required",
				   "perso required", "pin1 blocked", "illegal", "ready" };
static const char *app_type[] = { "unknown", "SIM", "USIM", "RUIM", "CSIM", "ISIM" };
static const char *pin_state[] = { "unknown", "enabled, not verified", "enabled, verified",
				   "disabled", "blocked", "permanently blocked" };

static int cmd_sim(void)
{
	uint8_t resp[2048];
	struct tlv t[16];
	int len, n;

	len = request(11, 0x002f, NULL, 0, resp, sizeof(resp));
	if (len < 0)
		return 1;
	n = parse(resp, len, t, 16);
	if (result(t, n))
		return 1;
	const struct tlv *cs = get(t, n, 0x10);
	if (!cs) {
		dump(t, n);
		return 0;
	}
	const uint8_t *p = cs->val + 8;
	int slots = *p++;
	for (int s = 0; s < slots; s++) {
		uint8_t cst = p[0], upin = p[1], err = p[4];
		int apps = p[5];

		printf("slot %d: card %s, upin %s, error %u, %d apps\n", s + 1,
		       cst < 3 ? card_state[cst] : "?", upin < 6 ? pin_state[upin] : "?", err, apps);
		p += 6;
		for (int a = 0; a < apps; a++) {
			uint8_t at = p[0], as = p[1], perso = p[2];
			int aidlen = p[6];
			const uint8_t *q = p + 7 + aidlen;

			printf("  app %d: %s, %s, perso %u, pin1 %s (%u retries)\n", a + 1,
			       at < 6 ? app_type[at] : "?", as < 8 ? app_state[as] : "?", perso,
			       q[1] < 6 ? pin_state[q[1]] : "?", q[2]);
			p = q + 7;
		}
	}
	return 0;
}

static const char *reg_state[] = { "not registered", "registered", "searching", "denied", "unknown" };
static const char *radio_if[] = { "none", "cdma1x", "evdo", "amps", "gsm", "umts", "?", "?", "lte",
				  "td-scdma", "?", "?", "nr5g" };

static int cmd_nas(void)
{
	uint8_t resp[2048];
	struct tlv t[24];
	int len, n;

	len = request(3, 0x0024, NULL, 0, resp, sizeof(resp));
	if (len < 0)
		return 1;
	n = parse(resp, len, t, 24);
	if (result(t, n))
		return 1;
	const struct tlv *ss = get(t, n, 0x01);
	if (ss && ss->len >= 5) {
		printf("registration: %s, cs attach %u, ps attach %u, network %u, radio:",
		       ss->val[0] < 5 ? reg_state[ss->val[0]] : "?", ss->val[1], ss->val[2], ss->val[3]);
		for (int i = 0; i < ss->val[4] && 5 + i < ss->len; i++) {
			uint8_t r = ss->val[5 + i];
			printf(" %s", r < 13 ? radio_if[r] : "?");
		}
		printf("\n");
	}
	const struct tlv *plmn = get(t, n, 0x12);
	if (plmn && plmn->len >= 5) {
		int dl = plmn->val[4];
		printf("plmn: %u-%02u \"%.*s\"\n", plmn->val[0] | plmn->val[1] << 8,
		       plmn->val[2] | plmn->val[3] << 8, dl, (const char *)plmn->val + 5);
	}
	return 0;
}

static int simple(unsigned int svc, uint16_t msg, const uint8_t *tlvs, uint16_t tlvlen)
{
	uint8_t resp[4096];
	struct tlv t[32];
	int len, n;

	len = request(svc, msg, tlvs, tlvlen, resp, sizeof(resp));
	if (len < 0)
		return 1;
	n = parse(resp, len, t, 32);
	if (result(t, n))
		return 1;
	dump(t, n);
	return 0;
}

static const char *op_mode[] = { "online", "low power", "factory test", "offline", "resetting",
				 "shutting down", "persistent low power", "mode-only low power" };

static int cmd_mode(void)
{
	uint8_t resp[512];
	struct tlv t[8];
	int len, n;

	len = request(2, 0x002d, NULL, 0, resp, sizeof(resp));
	if (len < 0)
		return 1;
	n = parse(resp, len, t, 8);
	if (result(t, n))
		return 1;
	const struct tlv *m = get(t, n, 0x01);
	if (m)
		printf("operating mode: %s (%u)\n", m->val[0] < 8 ? op_mode[m->val[0]] : "?", m->val[0]);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		goto usage;
	if (!strcmp(argv[1], "sim"))
		return cmd_sim();
	if (!strcmp(argv[1], "nas"))
		return cmd_nas();
	if (!strcmp(argv[1], "signal"))
		return simple(3, 0x004f, NULL, 0);
	if (!strcmp(argv[1], "mode"))
		return cmd_mode();
	if (!strcmp(argv[1], "online")) {
		const uint8_t tlv[] = { 0x01, 0x01, 0x00, 0x00 };
		if (simple(2, 0x002e, tlv, sizeof(tlv)))
			return 1;
		return cmd_mode();
	}
	if (!strcmp(argv[1], "raw") && argc > 3) {
		uint8_t tlv[256];
		int tl = 0;

		if (argc > 4)
			for (const char *h = argv[4]; h[0] && h[1] && tl < 256; h += 2) {
				char b[3] = { h[0], h[1], 0 };
				tlv[tl++] = strtoul(b, NULL, 16);
			}
		return simple(strtoul(argv[2], NULL, 0), strtoul(argv[3], NULL, 0), tlv, tl);
	}
usage:
	fprintf(stderr, "qmitool sim | nas | signal | mode | online | raw <svc> <msg> [tlvhex]\n");
	return 1;
}
