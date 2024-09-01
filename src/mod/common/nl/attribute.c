#include "mod/common/nl/attribute.h"

#include <linux/sort.h>
#include "common/constants.h"
#include "mod/common/log.h"
#include "mod/common/rfc6052.h"

#define SERIALIZED_SESSION_SIZE (		\
		sizeof(struct in6_addr)		\
		+ 2 * sizeof(struct in_addr)	\
		+ sizeof(__be32)		\
		+ 4 * sizeof(__be16)		\
)

static int validate_null(struct nlattr *attr, char const *name,
		struct jnl_state *state)
{
	if (!attr) {
		return jnls_err(state,
				"Invalid request: '%s' attribute is missing.",
				name);
	}

	return 0;
}

static int validate_len(struct nlattr *attr, char const *name,
		size_t expected_len, struct jnl_state *state)
{
	if (nla_len(attr) < expected_len) {
		return jnls_err(state, "Invalid request: %s has %d bytes instead of %zu.",
				name, nla_len(attr), expected_len);
	}

	return 0;
}

int jnla_get_u8(struct nlattr *attr, char const *name, __u8 *out,
		struct jnl_state *state)
{
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	*out = nla_get_u8(attr);
	return 0;
}

int jnla_get_u16(struct nlattr *attr, char const *name, __u16 *out,
		struct jnl_state *state)
{
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	*out = nla_get_u16(attr);
	return 0;
}

int jnla_get_u32(struct nlattr *attr, char const *name, __u32 *out,
		struct jnl_state *state)
{
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	*out = nla_get_u32(attr);
	return 0;
}

static int validate_str(char const *str, size_t max_size)
{
	size_t i;

	for (i = 0; i < max_size; i++)
		if (str[i] == '\0')
			return 0;

	return -EINVAL;
}

int jnla_get_str(struct nlattr *attr, char const *name, size_t size, char *out,
		struct jnl_state *state)
{
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;
	error = validate_str(nla_data(attr), size);
	if (error)
		return error;

	strcpy(out, nla_data(attr));
	return 0;
}

int jnla_get_addr6(struct nlattr *attr, char const *name, struct in6_addr *out,
		struct jnl_state *state)
{
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;
	error = validate_len(attr, name, sizeof(struct in6_addr), state);
	if (error)
		return error;

	memcpy(out, nla_data(attr), sizeof(*out));
	return 0;
}

int jnla_get_addr4(struct nlattr *attr, char const *name, struct in_addr *out,
		struct jnl_state *state)
{
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;
	error = validate_len(attr, name, sizeof(struct in_addr), state);
	if (error)
		return error;

	memcpy(out, nla_data(attr), sizeof(*out));
	return 0;
}

int jnla_get_prefix6(struct nlattr *attr, char const *name,
		struct ipv6_prefix *out, struct jnl_state *state)
{
	struct config_prefix6 tmp;
	int error;

	error = jnla_get_prefix6_optional(attr, name, &tmp, state);
	if (error)
		return error;

	if (!tmp.set) {
		return jnls_err(state,
				"Malformed %s: null despite being mandatory",
				name);
	}

	*out = tmp.prefix;
	return 0;
}

int jnla_get_prefix6_optional(struct nlattr *attr, char const *name,
		struct config_prefix6 *out, struct jnl_state *state)
{
	struct nlattr *attrs[JNLAP_COUNT];
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAP_MAX, attr, joolnl_prefix6_policy,
			name, state);
	if (error)
		return error;

	if (!attrs[JNLAP_LEN]) {
		return jnls_err(state,
				"Malformed %s: length attribute is missing",
				name);
	}
	if (!attrs[JNLAP_ADDR]) {
		out->set = false;
		return 0;
	}

	out->set = true;
	error = jnla_get_addr6(attrs[JNLAP_ADDR], "IPv6 prefix address",
			&out->prefix.addr, state);
	if (error)
		return error;
	out->prefix.len = nla_get_u8(attrs[JNLAP_LEN]);

	return prefix6_validate(&out->prefix, state);
}

int jnla_get_prefix4(struct nlattr *attr, char const *name,
		struct ipv4_prefix *out, struct jnl_state *state)
{
	struct config_prefix4 tmp;
	int error;

	error = jnla_get_prefix4_optional(attr, name, &tmp, state);
	if (error)
		return error;

	if (!tmp.set) {
		return jnls_err(state,
				"Malformed %s: null despite being mandatory",
				name);
	}

	*out = tmp.prefix;
	return 0;
}

int jnla_get_prefix4_optional(struct nlattr *attr, char const *name,
		struct config_prefix4 *out, struct jnl_state *state)
{
	struct nlattr *attrs[JNLAP_COUNT];
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAP_MAX, attr, joolnl_prefix4_policy,
			name, state);
	if (error)
		return error;

	if (!attrs[JNLAP_LEN]) {
		return jnls_err(state,
				"Malformed %s: length attribute is missing",
				name);
	}
	if (!attrs[JNLAP_ADDR]) {
		out->set = false;
		return 0;
	}

	out->set = true;
	error = jnla_get_addr4(attrs[JNLAP_ADDR], "IPv4 prefix address",
			&out->prefix.addr, state);
	if (error)
		return error;
	out->prefix.len = nla_get_u8(attrs[JNLAP_LEN]);

	return prefix4_validate(&out->prefix, state);
}

static int jnla_get_port(struct nlattr *attr, __u16 *out,
		struct jnl_state *state)
{
	int error;

	error = validate_null(attr, "port", state);
	if (error)
		return error;

	*out = nla_get_u16(attr);
	return 0;
}

int jnla_get_taddr6(struct nlattr *attr, char const *name,
		struct ipv6_transport_addr *out, struct jnl_state *state)
{
	struct nlattr *attrs[JNLAT_COUNT];
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAT_MAX, attr, joolnl_taddr6_policy,
			name, state);
	if (error)
		return error;

	error = jnla_get_addr6(attrs[JNLAT_ADDR], "IPv6 address",
			&out->l3, state);
	if (error)
		return error;
	return jnla_get_port(attrs[JNLAT_PORT], &out->l4, state);
}

int jnla_get_taddr4(struct nlattr *attr, char const *name,
		struct ipv4_transport_addr *out, struct jnl_state *state)
{
	struct nlattr *attrs[JNLAT_COUNT];
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAT_MAX, attr, joolnl_taddr4_policy,
			name, state);
	if (error)
		return error;

	error = jnla_get_addr4(attrs[JNLAT_ADDR], "IPv4 address",
			&out->l3, state);
	if (error)
		return error;
	return jnla_get_port(attrs[JNLAT_PORT], &out->l4, state);
}

int jnla_get_eam(struct nlattr *attr, char const *name, struct eamt_entry *eam,
		struct jnl_state *state)
{
	struct nlattr *attrs[JNLAE_COUNT];
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAE_MAX, attr, joolnl_eam_policy,
			name, state);
	if (error)
		return error;

	error = jnla_get_prefix6(attrs[JNLAE_PREFIX6], "IPv6 prefix",
			&eam->prefix6, state);
	if (error)
		return error;

	return jnla_get_prefix4(attrs[JNLAE_PREFIX4], "IPv4 prefix",
			&eam->prefix4, state);
}

int jnla_get_pool4(struct nlattr *attr, char const *name,
		struct pool4_entry *entry, struct jnl_state *state)
{
	struct nlattr *attrs[JNLAP4_COUNT];
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAP4_MAX, attr,
			joolnl_pool4_entry_policy, name, state);
	if (error)
		return error;

	memset(entry, 0, sizeof(*entry));

	if (attrs[JNLAP4_MARK])
		entry->mark = nla_get_u32(attrs[JNLAP4_MARK]);
	if (attrs[JNLAP4_ITERATIONS])
		entry->iterations = nla_get_u32(attrs[JNLAP4_ITERATIONS]);
	if (attrs[JNLAP4_FLAGS])
		entry->flags = nla_get_u8(attrs[JNLAP4_FLAGS]);

	error = jnla_get_u8(attrs[JNLAP4_PROTO], "Protocol",
			&entry->proto, state);
	if (error)
		return error;
	error = jnla_get_prefix4(attrs[JNLAP4_PREFIX], "IPv4 prefix",
			&entry->range.prefix, state);
	if (error)
		return error;
	error = jnla_get_u16(attrs[JNLAP4_PORT_MIN], "Minimum port",
			&entry->range.ports.min, state);
	if (error)
		return error;
	return jnla_get_u16(attrs[JNLAP4_PORT_MAX], "Maximum port",
			&entry->range.ports.max, state);
}

int jnla_get_bib(struct nlattr *attr, char const *name, struct bib_entry *entry,
		struct jnl_state *state)
{
	struct nlattr *attrs[JNLAB_COUNT];
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAB_MAX, attr,
			joolnl_bib_entry_policy, name, state);
	if (error)
		return error;

	memset(entry, 0, sizeof(*entry));

	error = jnla_get_taddr6(attrs[JNLAB_SRC6], "IPv6 transport address",
			&entry->addr6, state);
	if (error)
		return error;
	error = jnla_get_taddr4(attrs[JNLAB_SRC4], "IPv4 transport address",
			&entry->addr4, state);
	if (error)
		return error;
	error = jnla_get_u8(attrs[JNLAB_PROTO], "Protocol",
			&entry->l4_proto, state);
	if (error)
		return error;
	if (attrs[JNLAB_STATIC])
		entry->is_static = nla_get_u8(attrs[JNLAB_STATIC]);

	return 0;
}

static int get_timeout(struct bib_config *config, struct session_entry *entry,
		struct jnl_state *state)
{
	unsigned long timeout;

	switch (entry->proto) {
	case L4PROTO_TCP:
		switch (entry->timer_type) {
		case SESSION_TIMER_EST:
			timeout = config->ttl.tcp_est;
			break;
		case SESSION_TIMER_TRANS:
			timeout = config->ttl.tcp_trans;
			break;
		case SESSION_TIMER_SYN4:
			timeout = TCP_INCOMING_SYN;
			break;
		default:
			return jnls_err(state, "Unknown session timer: %u",
					entry->timer_type);
		}
		break;
	case L4PROTO_UDP:
		timeout = config->ttl.udp;
		break;
	case L4PROTO_ICMP:
		timeout = config->ttl.icmp;
		break;
	default:
		return jnls_err(state, "Unknown protocol: %u", entry->proto);
	}

	entry->timeout = msecs_to_jiffies(timeout);
	return 0;
}

#define READ_RAW(serialized, field)					\
	memcpy(&field, serialized, sizeof(field));			\
	serialized += sizeof(field);

int jnla_get_session_joold(struct nlattr *attr, char const *name,
		struct jool_globals *cfg, struct session_entry *se,
		struct jnl_state *state)
{
	__u8 *serialized;
	__be32 tmp32;
	__be16 tmp16;
	__u16 __tmp16;
	unsigned long expiration;
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	if (attr->nla_len < SERIALIZED_SESSION_SIZE)
		return jnls_err(state,
				"Invalid request: Session size (%u) < %zu",
				attr->nla_len, SERIALIZED_SESSION_SIZE);

	memset(se, 0, sizeof(*se));
	serialized = nla_data(attr);

	READ_RAW(serialized, se->src6.l3);
	READ_RAW(serialized, se->src4.l3);
	READ_RAW(serialized, se->dst4.l3);
	READ_RAW(serialized, tmp32);

	READ_RAW(serialized, tmp16);
	se->src6.l4 = ntohs(tmp16);
	READ_RAW(serialized, tmp16);
	se->src4.l4 = ntohs(tmp16);
	READ_RAW(serialized, tmp16);
	se->dst4.l4 = ntohs(tmp16);

	READ_RAW(serialized, tmp16);
	__tmp16 = ntohs(tmp16);
	se->proto = (__tmp16 >> 5) & 3;
	se->state = (__tmp16 >> 2) & 7;
	se->timer_type = __tmp16 & 3;

	error = __rfc6052_4to6(&cfg->pool6.prefix, &se->dst4.l3, &se->dst6.l3);
	if (error)
		return error;
	se->dst6.l4 = (se->proto == L4PROTO_ICMP) ? se->src6.l4 : se->dst4.l4;

	error = get_timeout(&cfg->nat64.bib, se, state);
	if (error)
		return error;

	expiration = msecs_to_jiffies(ntohl(tmp32));
	se->update_time = jiffies + expiration - se->timeout;
	se->has_stored = false;

	return 0;
}
EXPORT_UNIT_SYMBOL(jnla_get_session_joold)

int jnla_get_mapping_rule(struct nlattr *attr, char const *name,
		struct config_mapping_rule *_rule, struct jnl_state *state)
{
	struct nlattr *attrs[JNLAMR_COUNT];
	struct mapping_rule *rule;
	unsigned int suffix_len;
	unsigned int sid_len;
	unsigned int k;
	int error;

	error = validate_null(attr, name, state);
	if (error)
		return error;

	error = jnla_parse_nested(attrs, JNLAMR_MAX, attr, joolnl_mr_policy,
			name, state);
	if (error)
		return error;

	if (!attrs[JNLAMR_PREFIX4]) {
		_rule->set = false;
		return 0;
	}

	_rule->set = true;
	rule = &_rule->rule;

	error = jnla_get_prefix6(attrs[JNLAMR_PREFIX6], "IPv6 prefix",
			&rule->prefix6, state);
	if (error)
		return error;
	error = jnla_get_prefix4(attrs[JNLAMR_PREFIX4], "IPv4 prefix",
			&rule->prefix4, state);
	if (error)
		return error;
	error = jnla_get_u8(attrs[JNLAMR_EA_BITS_LENGTH], "EA-bits length",
			&rule->o, state);
	if (error)
		return error;
	rule->a = attrs[JNLAMR_a] ? nla_get_u8(attrs[JNLAMR_a]) : 6;

	if (rule->o > 48)
		return jnls_err(state, "EA-bits Length must not exceed 48.");

	suffix_len = 32u - rule->prefix4.len;
	sid_len = (suffix_len > rule->o) ? (suffix_len - rule->o) : 0;
	if (rule->prefix6.len + rule->o + sid_len > 128u) {
		return jnls_err(state, "The rule's IPv6 prefix length (%u) plus the EA-bits length (%u) plus the Subnet ID length (%u) exceed 128.",
				rule->prefix6.len, rule->o, sid_len);
	}

	if (rule->o + rule->prefix4.len <= 32u)
		return 0; /* a, k and m only matter when o + r > 32. */

	if (rule->a > 16)
		return jnls_err(state, "'a' must not exceed 16.");
	k = maprule_get_k(rule);
	if (rule->a + k > 16) {
		jnls_err(state, "a + k must not exceed 16.");
		jnls_err(state, "current values: a:%u k:%u", rule->a, k);
		return -EINVAL;
	}

	return 0;
}

static int u16_compare(const void *a, const void *b)
{
	return *(__u16 *)b - *(__u16 *)a;
}

static void u16_swap(void *a, void *b, int size)
{
	__u16 t = *(__u16 *)a;
	*(__u16 *)a = *(__u16 *)b;
	*(__u16 *)b = t;
}

static int validate_plateaus(struct mtu_plateaus *plateaus,
		struct jnl_state *state)
{
	__u16 *values = plateaus->values;
	unsigned int i, j;

	/* Sort descending. */
	sort(values, plateaus->count, sizeof(*values), u16_compare, u16_swap);

	/* Remove zeroes and duplicates. */
	for (i = 0, j = 1; j < plateaus->count; j++) {
		if (values[j] == 0)
			break;
		if (values[i] != values[j]) {
			i++;
			values[i] = values[j];
		}
	}

	if (values[0] == 0)
		return jnls_err(state, "The plateaus list contains nothing but zeroes.");

	/* Update. */
	plateaus->count = i + 1;
	return 0;
}

int jnla_get_plateaus(struct nlattr *root, struct mtu_plateaus *out,
		struct jnl_state *state)
{
	struct nlattr *attr;
	int rem;
	int error;

	error = validate_null(root, "MTU plateaus", state);
	if (error)
		return error;
	error = nla_validate(nla_data(root), nla_len(root), JNLAL_MAX,
			joolnl_plateau_list_policy, NULL);
	if (error)
		return error;

	out->count = 0;
	nla_for_each_nested(attr, root, rem) {
		if (out->count >= PLATEAUS_MAX)
			return jnls_err(state, "Too many plateaus.");

		out->values[out->count] = nla_get_u16(attr);
		out->count++;
	}

	return validate_plateaus(out, state);
}

int jnla_put_addr6(struct sk_buff *skb, int attrtype,
		struct in6_addr const *addr)
{
	return nla_put(skb, attrtype, sizeof(*addr), addr);
}

int jnla_put_addr4(struct sk_buff *skb, int attrtype,
		struct in_addr const *addr)
{
	return nla_put(skb, attrtype, sizeof(*addr), addr);
}

int jnla_put_prefix6(struct sk_buff *skb, int attrtype,
		struct ipv6_prefix const *prefix)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	if (prefix) {
		error = jnla_put_addr6(skb, JNLAP_ADDR, &prefix->addr)
		     || nla_put_u8(skb, JNLAP_LEN, prefix->len);
		if (error)
			goto cancel;
	} else {
		error = nla_put_u8(skb, JNLAP_LEN, 0);
		if (error)
			goto cancel;
	}

	nla_nest_end(skb, root);
	return 0;

cancel:
	nla_nest_cancel(skb, root);
	return error;
}

int jnla_put_prefix4(struct sk_buff *skb, int attrtype,
		struct ipv4_prefix const *prefix)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	if (prefix) {
		error = jnla_put_addr4(skb, JNLAP_ADDR, &prefix->addr)
		     || nla_put_u8(skb, JNLAP_LEN, prefix->len);
		if (error)
			goto cancel;
	} else {
		error = nla_put_u8(skb, JNLAP_LEN, 0);
		if (error)
			goto cancel;
	}

	nla_nest_end(skb, root);
	return 0;

cancel:
	nla_nest_cancel(skb, root);
	return error;
}

int jnla_put_taddr6(struct sk_buff *skb, int attrtype,
		struct ipv6_transport_addr const *taddr)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	error = jnla_put_addr6(skb, JNLAT_ADDR, &taddr->l3)
	     || nla_put_u16(skb, JNLAT_PORT, taddr->l4);
	if (error) {
		nla_nest_cancel(skb, root);
		return error;
	}

	nla_nest_end(skb, root);
	return 0;
}

int jnla_put_taddr4(struct sk_buff *skb, int attrtype,
		struct ipv4_transport_addr const *taddr)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	error = jnla_put_addr4(skb, JNLAT_ADDR, &taddr->l3)
	     || nla_put_u16(skb, JNLAT_PORT, taddr->l4);
	if (error) {
		nla_nest_cancel(skb, root);
		return error;
	}

	nla_nest_end(skb, root);
	return 0;
}

int jnla_put_eam(struct sk_buff *skb, int attrtype,
		struct eamt_entry const *eam)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	error = jnla_put_prefix6(skb, JNLAE_PREFIX6, &eam->prefix6)
	     || jnla_put_prefix4(skb, JNLAE_PREFIX4, &eam->prefix4);
	if (error) {
		nla_nest_cancel(skb, root);
		return error;
	}

	nla_nest_end(skb, root);
	return 0;
}

int jnla_put_pool4(struct sk_buff *skb, int attrtype,
		struct pool4_entry const *entry)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	error = nla_put_u32(skb, JNLAP4_MARK, entry->mark)
		|| nla_put_u32(skb, JNLAP4_ITERATIONS, entry->iterations)
		|| nla_put_u8(skb, JNLAP4_FLAGS, entry->flags)
		|| nla_put_u8(skb, JNLAP4_PROTO, entry->proto)
		|| jnla_put_prefix4(skb, JNLAP4_PREFIX, &entry->range.prefix)
		|| nla_put_u16(skb, JNLAP4_PORT_MIN, entry->range.ports.min)
		|| nla_put_u16(skb, JNLAP4_PORT_MAX, entry->range.ports.max);
	if (error) {
		nla_nest_cancel(skb, root);
		return error;
	}

	nla_nest_end(skb, root);
	return 0;
}

int jnla_put_bib(struct sk_buff *skb, int attrtype, struct bib_entry const *bib)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	error = jnla_put_taddr6(skb, JNLAB_SRC6, &bib->addr6)
		|| jnla_put_taddr4(skb, JNLAB_SRC4, &bib->addr4)
		|| nla_put_u8(skb, JNLAB_PROTO, bib->l4_proto)
		|| nla_put_u8(skb, JNLAB_STATIC, bib->is_static);
	if (error) {
		nla_nest_cancel(skb, root);
		return -EMSGSIZE;
	}

	nla_nest_end(skb, root);
	return 0;
}

int jnla_put_session(struct sk_buff *skb, int attrtype,
		struct session_entry const *entry)
{
	struct nlattr *root;
	unsigned long dying_time;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	dying_time = entry->update_time + entry->timeout;
	dying_time = (dying_time > jiffies)
			? jiffies_to_msecs(dying_time - jiffies)
			: 0;
	if (dying_time > MAX_U32)
		dying_time = MAX_U32;

	error = jnla_put_taddr6(skb, JNLASE_SRC6, &entry->src6)
		|| jnla_put_taddr6(skb, JNLASE_DST6, &entry->dst6)
		|| jnla_put_taddr4(skb, JNLASE_SRC4, &entry->src4)
		|| jnla_put_taddr4(skb, JNLASE_DST4, &entry->dst4)
		|| nla_put_u8(skb, JNLASE_PROTO, entry->proto)
		|| nla_put_u8(skb, JNLASE_STATE, entry->state)
		|| nla_put_u8(skb, JNLASE_TIMER, entry->timer_type)
		|| nla_put_u32(skb, JNLASE_EXPIRATION, dying_time);
	if (error) {
		nla_nest_cancel(skb, root);
		return error;
	}

	nla_nest_end(skb, root);
	return 0;
}

#define ADD_RAW(buffer, offset, content)				\
	memcpy(buffer + offset, &content, sizeof(content));		\
	offset += sizeof(content)

int jnla_put_session_joold(struct sk_buff *skb, int attrtype,
		struct session_entry const *entry)
{
	__u8 buffer[SERIALIZED_SESSION_SIZE];
	size_t offset;
	unsigned long dying_time;
	__be32 tmp32;
	__be16 tmp16;

	/*
	 * The session object is huge, and joold wants to fit as many sessions
	 * as possible in one single packet.
	 * Therefore, instead of adding each field as a Netlink attribute,
	 * we'll do some low level byte hacking.
	 */

	offset = 0;

	/* 128 bit fields */
	ADD_RAW(buffer, offset, entry->src6.l3);
	/* Skip dst6; it can be inferred from dst4. */

	/* 32 bit fields */
	ADD_RAW(buffer, offset, entry->src4.l3);
	ADD_RAW(buffer, offset, entry->dst4.l3);

	dying_time = entry->update_time + entry->timeout;
	dying_time = (dying_time > jiffies)
			? jiffies_to_msecs(dying_time - jiffies)
			: 0;
	if (dying_time > MAX_U32)
		dying_time = MAX_U32;

	tmp32 = htonl(dying_time);
	ADD_RAW(buffer, offset, tmp32);

	/* 16 bit fields */
	tmp16 = htons(entry->src6.l4);
	ADD_RAW(buffer, offset, tmp16);
	tmp16 = htons(entry->src4.l4);
	ADD_RAW(buffer, offset, tmp16);
	tmp16 = htons(entry->dst4.l4);
	ADD_RAW(buffer, offset, tmp16);

	/* Well, this fits in a byte, but use 2 to avoid slop */
	tmp16 = htons(
		(entry->proto << 5) /* 2 bits */
		| (entry->state << 2) /* 3 bits */
		| entry->timer_type /* 2 bits */
	);
	ADD_RAW(buffer, offset, tmp16);

	return nla_put(skb, attrtype, sizeof(buffer), buffer);
}
EXPORT_UNIT_SYMBOL(jnla_put_session_joold)

int jnla_put_mapping_rule(struct sk_buff *skb, int attrtype,
		struct config_mapping_rule const *rule)
{
	struct nlattr *root;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	if (rule->set) {
		error = jnla_put_prefix6(skb, JNLAMR_PREFIX6, &rule->rule.prefix6);
		if (error)
			goto cancel;
		error = jnla_put_prefix4(skb, JNLAMR_PREFIX4, &rule->rule.prefix4);
		if (error)
			goto cancel;
		error = nla_put_u8(skb, JNLAMR_EA_BITS_LENGTH, rule->rule.o);
		if (error)
			goto cancel;
		error = nla_put_u8(skb, JNLAMR_a, rule->rule.a);
		if (error)
			goto cancel;
	} else {
		error = jnla_put_prefix6(skb, JNLAMR_PREFIX6, NULL);
		if (error)
			goto cancel;
	}

	nla_nest_end(skb, root);
	return 0;

cancel:
	nla_nest_cancel(skb, root);
	return error;
}

int jnla_put_plateaus(struct sk_buff *skb, int attrtype,
		struct mtu_plateaus const *plateaus)
{
	struct nlattr *root;
	unsigned int i;
	int error;

	root = nla_nest_start(skb, attrtype);
	if (!root)
		return -EMSGSIZE;

	for (i = 0; i < plateaus->count; i++) {
		error = nla_put_u16(skb, JNLAL_ENTRY, plateaus->values[i]);
		if (error) {
			nla_nest_cancel(skb, root);
			return error;
		}
	}

	nla_nest_end(skb, root);
	return 0;
}

int jnla_parse_nested(struct nlattr *tb[], int maxtype,
		const struct nlattr *nla, const struct nla_policy *policy,
		char const *name, struct jnl_state *state)
{
	int error;
	struct netlink_ext_ack extack;

	error = nla_parse_nested(tb, maxtype, nla, policy, &extack);
	if (error)
		jnls_err(state, "The '%s' attribute is malformed: %s", name, extack._msg);

	return error;
}


void report_put_failure(struct jnl_state *state)
{
	jnls_err(state, "The allocated Netlink packet is too small to contain the response. This might be a bug; please report it. PAGE_SIZE is %lu.",
			PAGE_SIZE);
}
