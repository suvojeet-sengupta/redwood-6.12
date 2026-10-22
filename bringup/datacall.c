// SPDX-License-Identifier: GPL-2.0
/*
 * Bring up an IPv4 mobile data call through IPA and rmnet, the way netmgrd
 * does on the 5.4 vendor, for the bring-up shell:
 *
 *  - rmnet_ipa0: QMAP ingress/egress format and mux channel 1
 *  - rmnet_data0 on top of it (rmnet, mux id 1)
 *  - QMI: WDA data format (raw IP, QMAP, embedded endpoint 1), WDS bind
 *    to mux 1, start the call and read the runtime settings
 *  - address, route and MTU on rmnet_data0
 *
 * It keeps running: the call ends when its WDS client goes away.
 *
 *   datacall <apn>
 */
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/ethtool.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/qrtr.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include "libqrtr.h"

#define RMNET_IOCTL_EXTENDED			0x89fd
#define RMNET_IOCTL_GET_EPID			0x0003
#define RMNET_IOCTL_ADD_MUX_CHANNEL		0x0005
#define RMNET_IOCTL_GET_EP_PAIR			0x0010
#define RMNET_IOCTL_SET_EGRESS_DATA_FORMAT	0x0006
#define RMNET_IOCTL_SET_INGRESS_DATA_FORMAT	0x0007
#define RMNET_IOCTL_EGRESS_FORMAT_MAP		(1 << 1)
#define RMNET_IOCTL_EGRESS_FORMAT_MUXING	(1 << 3)
#define RMNET_IOCTL_INGRESS_FORMAT_MAP		(1 << 1)
#define RMNET_IOCTL_INGRESS_FORMAT_DEMUXING	(1 << 3)

struct rmnet_ioctl_extended_s {
	uint32_t extended_ioctl;
	union {
		uint32_t data;
		struct {
			uint32_t mux_id;
			char vchannel_name[IFNAMSIZ];
		} rmnet_mux_val;
		struct {
			uint32_t consumer_pipe_num;
			uint32_t producer_pipe_num;
		} ipa_ep_pair;
		uint8_t pad[64];
	} u;
};

#define IPA_IF		"rmnet_ipa0"
#define DATA_IF		"rmnet_data0"
#define MUX_ID		1
#define EP_EMBEDDED	4

static uint32_t ep_iface = 1;
static uint32_t pipe_cons, pipe_prod;

static int ipa_ext(int sock, uint32_t cmd, struct rmnet_ioctl_extended_s *ext)
{
	struct ifreq ifr = { 0 };

	ext->extended_ioctl = cmd;
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", IPA_IF);
	ifr.ifr_ifru.ifru_data = (void *)ext;
	if (ioctl(sock, RMNET_IOCTL_EXTENDED, &ifr) < 0) {
		fprintf(stderr, "%s ioctl 0x%x: %s\n", IPA_IF, cmd, strerror(errno));
		return -1;
	}
	return 0;
}

static int set_up(int sock, const char *name)
{
	struct ifreq ifr = { 0 };

	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
	if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
		return -1;
	ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
	return ioctl(sock, SIOCSIFFLAGS, &ifr);
}

static int setup_ipa(int sock)
{
	struct rmnet_ioctl_extended_s ext;

	memset(&ext, 0, sizeof(ext));
	ext.u.data = RMNET_IOCTL_INGRESS_FORMAT_MAP | RMNET_IOCTL_INGRESS_FORMAT_DEMUXING;
	if (ipa_ext(sock, RMNET_IOCTL_SET_INGRESS_DATA_FORMAT, &ext))
		fprintf(stderr, "going on: the WAN pipe is already set up\n");
	memset(&ext, 0, sizeof(ext));
	ext.u.data = RMNET_IOCTL_EGRESS_FORMAT_MAP | RMNET_IOCTL_EGRESS_FORMAT_MUXING;
	if (ipa_ext(sock, RMNET_IOCTL_SET_EGRESS_DATA_FORMAT, &ext))
		fprintf(stderr, "going on: the WAN pipe is already set up\n");
	memset(&ext, 0, sizeof(ext));
	ext.u.rmnet_mux_val.mux_id = MUX_ID;
	snprintf(ext.u.rmnet_mux_val.vchannel_name, IFNAMSIZ, "%s", DATA_IF);
	if (ipa_ext(sock, RMNET_IOCTL_ADD_MUX_CHANNEL, &ext))
		return -1;
	if (set_up(sock, IPA_IF) < 0) {
		fprintf(stderr, "%s up: %s\n", IPA_IF, strerror(errno));
		return -1;
	}
	memset(&ext, 0, sizeof(ext));
	if (!ipa_ext(sock, RMNET_IOCTL_GET_EPID, &ext))
		ep_iface = ext.u.data;
	memset(&ext, 0, sizeof(ext));
	if (ipa_ext(sock, RMNET_IOCTL_GET_EP_PAIR, &ext))
		return -1;
	pipe_cons = ext.u.ipa_ep_pair.consumer_pipe_num;
	pipe_prod = ext.u.ipa_ep_pair.producer_pipe_num;
	printf("endpoint: embedded %u, consumer pipe %u, producer pipe %u\n",
	       ep_iface, pipe_cons, pipe_prod);
	return 0;
}

static void nla_put(struct nlmsghdr *n, int type, const void *data, int len)
{
	struct rtattr *rta = (struct rtattr *)((char *)n + NLMSG_ALIGN(n->nlmsg_len));

	rta->rta_type = type;
	rta->rta_len = RTA_LENGTH(len);
	if (len)
		memcpy(RTA_DATA(rta), data, len);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(rta->rta_len);
}

static struct rtattr *nla_nest(struct nlmsghdr *n, int type)
{
	struct rtattr *rta = (struct rtattr *)((char *)n + NLMSG_ALIGN(n->nlmsg_len));

	nla_put(n, type, NULL, 0);
	return rta;
}

static void nla_end(struct nlmsghdr *n, struct rtattr *nest)
{
	nest->rta_len = (char *)n + n->nlmsg_len - (char *)nest;
}

/*
 * Nothing on this path fills in TX checksums or segments for rmnet_data0,
 * so leave both to the stack.
 */
static void no_offloads(int sock)
{
	static const unsigned int cmds[] = {
		ETHTOOL_STXCSUM, ETHTOOL_STSO, ETHTOOL_SGSO, ETHTOOL_SSG,
	};
	struct ethtool_value ev;
	struct ifreq ifr;
	unsigned int i;

	for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		memset(&ifr, 0, sizeof(ifr));
		snprintf(ifr.ifr_name, IFNAMSIZ, "%s", DATA_IF);
		ev.cmd = cmds[i];
		ev.data = 0;
		ifr.ifr_data = (void *)&ev;
		ioctl(sock, SIOCETHTOOL, &ifr);
	}
}

static int add_rmnet(void)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
		char buf[512];
	} req;
	struct { uint32_t flags, mask; } flags = { 0, 0 };
	uint32_t link = if_nametoindex(IPA_IF);
	uint16_t mux = MUX_ID;
	struct rtattr *li, *data;
	char resp[512];
	int fd, len;

	if (if_nametoindex(DATA_IF))
		return 0;
	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.n.nlmsg_type = RTM_NEWLINK;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
	req.i.ifi_family = AF_UNSPEC;
	nla_put(&req.n, IFLA_IFNAME, DATA_IF, strlen(DATA_IF) + 1);
	nla_put(&req.n, IFLA_LINK, &link, sizeof(link));
	li = nla_nest(&req.n, IFLA_LINKINFO);
	nla_put(&req.n, IFLA_INFO_KIND, "rmnet", 6);
	data = nla_nest(&req.n, IFLA_INFO_DATA);
	nla_put(&req.n, IFLA_RMNET_MUX_ID, &mux, sizeof(mux));
	nla_put(&req.n, IFLA_RMNET_FLAGS, &flags, sizeof(flags));
	nla_end(&req.n, data);
	nla_end(&req.n, li);

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0 || send(fd, &req, req.n.nlmsg_len, 0) < 0) {
		fprintf(stderr, "netlink: %s\n", strerror(errno));
		return -1;
	}
	len = recv(fd, resp, sizeof(resp), 0);
	close(fd);
	if (len >= (int)NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
		struct nlmsghdr *h = (struct nlmsghdr *)resp;
		struct nlmsgerr *e = NLMSG_DATA(h);

		if (h->nlmsg_type == NLMSG_ERROR && e->error) {
			fprintf(stderr, "create %s: %s\n", DATA_IF, strerror(-e->error));
			return -1;
		}
	}
	return 0;
}

struct qmi {
	int sock;
	uint32_t node, port;
	uint16_t txn;
};

static int qmi_open(struct qmi *q, unsigned int svc)
{
	struct qrtr_packet pkt;
	struct sockaddr_qrtr sq;
	socklen_t sl = sizeof(sq);
	char buf[256];
	int len, found = 0;

	q->sock = qrtr_open(0);
	q->txn = 1;
	if (q->sock < 0 || qrtr_new_lookup(q->sock, svc, 0, 0) < 0)
		return -1;
	while (qrtr_poll(q->sock, 2000) > 0) {
		len = recvfrom(q->sock, buf, sizeof(buf), 0, (struct sockaddr *)&sq, &sl);
		if (len < 0 || qrtr_decode(&pkt, buf, len, &sq) < 0)
			break;
		if (pkt.type != QRTR_TYPE_NEW_SERVER)
			continue;
		if (!pkt.service && !pkt.node && !pkt.port)
			break;
		if (pkt.service == svc && !found) {
			q->node = pkt.node;
			q->port = pkt.port;
			found = 1;
		}
	}
	if (!found)
		fprintf(stderr, "QMI service %u not found\n", svc);
	return found ? 0 : -1;
}

static uint8_t *tlv_add(uint8_t *p, uint8_t type, const void *val, uint16_t len)
{
	p[0] = type;
	p[1] = len & 0xff;
	p[2] = len >> 8;
	memcpy(p + 3, val, len);
	return p + 3 + len;
}

static int u32tlv(uint8_t *p, uint8_t type, uint32_t v)
{
	return tlv_add(p, type, &v, 4) - p;
}

/* Send a request and wait for its response; returns the response length. */
static int qmi_req(struct qmi *q, uint16_t msg, const uint8_t *tlvs, uint16_t tlvlen,
		   uint8_t *resp, int resplen)
{
	uint8_t req[512];
	uint32_t node, port;
	int len;

	req[0] = 0;
	req[1] = q->txn & 0xff;
	req[2] = q->txn >> 8;
	req[3] = msg & 0xff;
	req[4] = msg >> 8;
	req[5] = tlvlen & 0xff;
	req[6] = tlvlen >> 8;
	memcpy(req + 7, tlvs, tlvlen);
	if (qrtr_sendto(q->sock, q->node, q->port, req, 7 + tlvlen) < 0)
		return -1;
	for (;;) {
		if (qrtr_poll(q->sock, 30000) <= 0) {
			fprintf(stderr, "QMI 0x%04x: no response\n", msg);
			return -1;
		}
		len = qrtr_recvfrom(q->sock, resp, resplen, &node, &port);
		if (len >= 7 && node == q->node && port == q->port && resp[0] == 2 &&
		    (resp[1] | resp[2] << 8) == q->txn && (resp[3] | resp[4] << 8) == msg)
			break;
	}
	q->txn++;
	return len;
}

static const uint8_t *tlv_get(const uint8_t *resp, int len, uint8_t type, uint16_t *tl)
{
	int off = 7, end = 7 + (resp[5] | resp[6] << 8);

	if (end > len)
		end = len;
	while (off + 3 <= end) {
		uint16_t l = resp[off + 1] | resp[off + 2] << 8;

		if (resp[off] == type) {
			if (tl)
				*tl = l;
			return resp + off + 3;
		}
		off += 3 + l;
	}
	return NULL;
}

static int qmi_check(const char *what, const uint8_t *resp, int len)
{
	const uint8_t *r = tlv_get(resp, len, 0x02, NULL);
	uint16_t res, err;

	if (!r) {
		fprintf(stderr, "%s: no result\n", what);
		return -1;
	}
	res = r[0] | r[1] << 8;
	err = r[2] | r[3] << 8;
	if (res) {
		fprintf(stderr, "%s: QMI error 0x%04x\n", what, err);
		return -1;
	}
	printf("%s: ok\n", what);
	return 0;
}

static uint32_t get_u32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void ip4(char *out, uint32_t v)
{
	sprintf(out, "%u.%u.%u.%u", v >> 24, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

static int set_addr(int sock, unsigned long req, uint32_t v)
{
	struct ifreq ifr = { 0 };
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", DATA_IF);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(v);
	return ioctl(sock, req, &ifr);
}

int main(int argc, char **argv)
{
	uint8_t t[256], *p, resp[2048];
	struct qmi dpm, wda, wds;
	uint32_t addr = 0, gw = 0, mask = 0, dns1 = 0, dns2 = 0, mtu = 0;
	const uint8_t *v;
	char a[16], g[16], m[16], d1[16], d2[16];
	int sock, len;

	if (argc < 2) {
		fprintf(stderr, "datacall <apn>\n");
		return 1;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (setup_ipa(sock) || add_rmnet())
		return 1;
	no_offloads(sock);
	printf("%s on %s, mux %d\n", DATA_IF, IPA_IF, MUX_ID);

	if (qmi_open(&dpm, 47) || qmi_open(&wda, 26) || qmi_open(&wds, 1))
		return 1;

	p = t;
	{
		uint8_t port[1 + 16];
		uint32_t v[4] = { EP_EMBEDDED, ep_iface, pipe_cons, pipe_prod };

		port[0] = 1;
		memcpy(port + 1, v, sizeof(v));
		p = tlv_add(p, 0x11, port, sizeof(port));
	}
	len = qmi_req(&dpm, 0x0020, t, p - t, resp, sizeof(resp));
	if (len < 0 || qmi_check("DPM open port", resp, len))
		fprintf(stderr, "going on without DPM\n");

	p = t;
	p += u32tlv(p, 0x11, 2);		/* link layer: raw IP */
	p += u32tlv(p, 0x12, 5);		/* UL aggregation: QMAP */
	p += u32tlv(p, 0x13, 5);		/* DL aggregation: QMAP */
	p += u32tlv(p, 0x15, 1);		/* DL max datagrams */
	p += u32tlv(p, 0x16, 2048);		/* DL max datagram size */
	{
		uint32_t ep[2] = { EP_EMBEDDED, ep_iface };
		p = tlv_add(p, 0x17, ep, sizeof(ep));
	}
	len = qmi_req(&wda, 0x0020, t, p - t, resp, sizeof(resp));
	if (len < 0 || qmi_check("WDA set data format", resp, len))
		fprintf(stderr, "going on: IPA already gave the modem its QMAP format\n");

	p = t;
	{
		uint32_t ep[2] = { EP_EMBEDDED, ep_iface };
		uint8_t mux = MUX_ID;
		p = tlv_add(p, 0x10, ep, sizeof(ep));
		p = tlv_add(p, 0x11, &mux, 1);
	}
	len = qmi_req(&wds, 0x00a2, t, p - t, resp, sizeof(resp));
	if (len < 0 || qmi_check("WDS bind mux data port", resp, len))
		return 1;

	p = t;
	{
		uint8_t fam = 4;
		p = tlv_add(p, 0x14, argv[1], strlen(argv[1]));
		p = tlv_add(p, 0x19, &fam, 1);
	}
	len = qmi_req(&wds, 0x0020, t, p - t, resp, sizeof(resp));
	if (len < 0)
		return 1;
	if (qmi_check("WDS start network", resp, len)) {
		uint16_t l;
		v = tlv_get(resp, len, 0x10, &l);
		if (v && l >= 2)
			fprintf(stderr, "call end reason %u\n", v[0] | v[1] << 8);
		v = tlv_get(resp, len, 0x11, &l);
		if (v && l >= 4)
			fprintf(stderr, "verbose call end reason type %u reason %u\n",
				v[0] | v[1] << 8, v[2] | v[3] << 8);
		return 1;
	}

	p = t;
	p += u32tlv(p, 0x10, 0x10 | 0x100 | 0x200 | 0x2000);
	len = qmi_req(&wds, 0x002d, t, p - t, resp, sizeof(resp));
	if (len < 0 || qmi_check("WDS get runtime settings", resp, len))
		return 1;
	if ((v = tlv_get(resp, len, 0x1e, NULL)))
		addr = get_u32(v);
	if ((v = tlv_get(resp, len, 0x20, NULL)))
		gw = get_u32(v);
	if ((v = tlv_get(resp, len, 0x21, NULL)))
		mask = get_u32(v);
	if ((v = tlv_get(resp, len, 0x15, NULL)))
		dns1 = get_u32(v);
	if ((v = tlv_get(resp, len, 0x16, NULL)))
		dns2 = get_u32(v);
	if ((v = tlv_get(resp, len, 0x29, NULL)))
		mtu = get_u32(v);
	ip4(a, addr); ip4(g, gw); ip4(m, mask); ip4(d1, dns1); ip4(d2, dns2);
	printf("ip %s/%s gw %s dns %s %s mtu %u\n", a, m, g, d1, d2, mtu);

	if (set_addr(sock, SIOCSIFADDR, addr) || set_addr(sock, SIOCSIFNETMASK, mask ? mask : 0xfffffff8))
		fprintf(stderr, "address: %s\n", strerror(errno));
	if (mtu) {
		struct ifreq ifr = { 0 };
		snprintf(ifr.ifr_name, IFNAMSIZ, "%s", DATA_IF);
		ifr.ifr_mtu = mtu;
		ioctl(sock, SIOCSIFMTU, &ifr);
	}
	if (set_up(sock, DATA_IF) < 0)
		fprintf(stderr, "%s up: %s\n", DATA_IF, strerror(errno));
	{
		struct rtentry rt = { 0 };
		struct sockaddr_in *dst = (struct sockaddr_in *)&rt.rt_dst;
		struct sockaddr_in *msk = (struct sockaddr_in *)&rt.rt_genmask;

		dst->sin_family = AF_INET;
		msk->sin_family = AF_INET;
		rt.rt_flags = RTF_UP;
		rt.rt_dev = DATA_IF;
		if (ioctl(sock, SIOCADDRT, &rt) < 0)
			fprintf(stderr, "default route: %s\n", strerror(errno));
	}
	{
		FILE *f = fopen("/etc/resolv.conf", "w");
		if (f) {
			fprintf(f, "nameserver %s\nnameserver %s\n", d1, d2);
			fclose(f);
		}
	}
	printf("data call up on %s, keeping it open\n", DATA_IF);
	for (;;)
		pause();
}
