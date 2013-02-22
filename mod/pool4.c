#include "nat64/mod/pool4.h"
#include "nat64/comm/constants.h"
#include "nat64/comm/str_utils.h"

#include <linux/slab.h>


/** Rename for the type of the port list below. */
#define port_list list_head
#define address_list list_head

/**
 * A port which is known to be in the pool; available for borrowal.
 */
struct free_port {
	/** The port number. */
	__u16 port;
	/** Next port within the list of free ones (see addr_section.free_ports). */
	struct list_head next;
};

/**
 * A range of ports within an address.
 */
struct addr_section {
	/** Next available (and never before used) port. */
	__u32 next_port;
	/**
	 * Maximum value "next_port" can hold. If this value has been reached and next_port needs to
	 * be incremented, the section has been exhausted.
	 */
	__u32 max_port;
	/**
	 * List of available (and previously used) ports. Contains structs of type free_port.
	 * It's a list because the FIFO behavior is ideal.
	 */
	struct port_list free_ports;
};

struct protocol_ids {
	/** The address's odd ports from the range 0-1023. */
	struct addr_section odd_low;
	/** The address's even ports from the range 0-1023. */
	struct addr_section even_low;
	/** The address's odd ports from the range 1024-65535. */
	struct addr_section odd_high;
	/** The address's even ports from the range 1024-65535. */
	struct addr_section even_high;
};

/**
 * An address within the pool, along with its ports.
 */
struct pool_node {
	/** The address itself. */
	struct in_addr address;

	struct protocol_ids udp;
	struct protocol_ids tcp;
	struct protocol_ids icmp;

	/** Next address within the pool (since they are linked listed; see pool). */
	struct list_head next;
};

static struct address_list pool;
static DEFINE_SPINLOCK(pool_lock);


/**
 * Assumes that pool has already been locked (pool->lock).
 */
static struct pool_node *get_pool_node(struct in_addr *address)
{
	struct pool_node *node;

	if (list_empty(&pool)) {
		log_err(ERR_POOL4_EMPTY, "The IPv4 pool is empty.");
		return NULL;
	}

	list_for_each_entry(node, &pool, next)
		if (ipv4_addr_equals(&node->address, address))
			return node;

	return NULL;
}

static struct protocol_ids *get_ids(struct pool_node *node, u_int8_t l4protocol)
{
	switch (l4protocol) {
	case IPPROTO_UDP:
		return &node->udp;
	case IPPROTO_TCP:
		return &node->tcp;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		return &node->icmp;
	}

	log_crit(ERR_L4PROTO, "Unsupported transport protocol: %u.", l4protocol);
	return NULL;
}

/**
 * Assumes that node's pool has already been locked (pool->lock).
 */
static struct addr_section *get_section(struct protocol_ids *ids, __u16 l4_id)
{
	if (!ids)
		return NULL;

	if (l4_id < 1024)
		return (l4_id % 2 == 0) ? &ids->even_low : &ids->odd_low;
	else
		return (l4_id % 2 == 0) ? &ids->even_high : &ids->odd_high;
}

/**
 * Assumes that section's pool has already been locked (pool->lock). TODO
 */
static bool extract_any_port(struct addr_section *section, __u16 *port)
{
	if (!section)
		return NULL;

	if (!list_empty(&section->free_ports)) {
		// Reuse it.
		struct free_port *node = list_entry(section->free_ports.next, struct free_port, next);
		*port = node->port;

		list_del(&node->next);
		kfree(node);

		return true;
	}

	if (section->next_port > section->max_port)
		return false;

	*port = section->next_port;
	section->next_port += 2;
	return true;
}

static bool load_defaults(void)
{
	unsigned char *addrs[] = POOL4_DEF;
	struct in_addr addr;
	int i;

	for (i = 0; i < ARRAY_SIZE(addrs); i++) {
		if (str_to_addr4(addrs[i], &addr) != ERR_SUCCESS) {
			log_err(ERR_POOL4_INVALID_DEFAULT, "Address in headers is malformed: %s.", addrs[i]);
			goto failure;
		}
		if (pool4_register(&addr) != ERR_SUCCESS)
			goto failure;
	}

	return true;

failure:
	pool4_destroy();
	return false;
}

bool pool4_init(bool defaults)
{
	INIT_LIST_HEAD(&pool);

	if (defaults && !load_defaults())
		return false;

	return true;
}

/**
 * Assumes that "pool_lock" has already been locked.
 */
static void destroy_section(struct addr_section *section)
{
	struct list_head *node;
	struct free_port *port;

	while (!list_empty(&section->free_ports)) {
		node = section->free_ports.next;
		port = container_of(node, struct free_port, next);
		list_del(node);
		kfree(port);
	}
}

/**
 * Assumes that "pool_lock" has already been locked.
 */
static void destroy_pool_node(struct pool_node *node)
{
	struct protocol_ids *protos[] = { &node->udp, &node->tcp, &node->icmp };
	int i;

	list_del(&node->next);
	for (i = 0; i < ARRAY_SIZE(protos); i++) {
		destroy_section(&protos[i]->odd_low);
		destroy_section(&protos[i]->even_low);
		destroy_section(&protos[i]->odd_high);
		destroy_section(&protos[i]->even_high);
	}
	kfree(node);
}

void pool4_destroy(void)
{
	struct list_head *head;
	struct pool_node *node;

	spin_lock_bh(&pool_lock);
	while (!list_empty(&pool)) {
		head = pool.next;
		node = container_of(head, struct pool_node, next);
		destroy_pool_node(node);
	}
	spin_unlock_bh(&pool_lock);
}

static void init_section(struct addr_section *section, __u32 next_port, __u32 max_port)
{
	section->next_port = next_port;
	section->max_port = max_port;
	INIT_LIST_HEAD(&section->free_ports);
}

static void init_protocol_ids(struct protocol_ids *ids)
{
	init_section(&ids->odd_low, 1, 1023);
	init_section(&ids->even_low, 0, 1022);
	init_section(&ids->odd_high, 1025, 65535);
	init_section(&ids->even_high, 1024, 65534);
}

enum error_code pool4_register(struct in_addr *address)
{
	struct pool_node *old_node, *new_node;

	if (!address) {
		log_err(ERR_NULL, "NULL cannot be inserted to the pool.");
		return ERR_NULL;
	}

	new_node = kmalloc(sizeof(struct pool_node), GFP_ATOMIC);
	if (!new_node) {
		log_err(ERR_ALLOC_FAILED, "Allocation of IPv4 pool node failed.");
		return ERR_ALLOC_FAILED;
	}

	new_node->address = *address;
	init_protocol_ids(&new_node->udp);
	init_protocol_ids(&new_node->tcp);
	init_protocol_ids(&new_node->icmp);

	spin_lock_bh(&pool_lock);

	list_for_each_entry(old_node, &pool, next) {
		if (ipv4_addr_equals(&old_node->address, address)) {
			spin_unlock_bh(&pool_lock);
			kfree(new_node);
			log_err(ERR_POOL4_REINSERT, "The %pI4 address already belongs to the pool.", address);
			return ERR_POOL4_REINSERT;
		}
	}

	// "add to head->prev" = "add to the end of the list".
	list_add(&new_node->next, pool.prev);

	spin_unlock_bh(&pool_lock);
	return ERR_SUCCESS;
}

enum error_code pool4_remove(struct in_addr *address)
{
	struct pool_node *node;

	if (!address) {
		log_err(ERR_NULL, "NULL is not a valid address.");
		return ERR_NULL;
	}

	spin_lock_bh(&pool_lock);

	node = get_pool_node(address);
	if (!node) {
		spin_unlock_bh(&pool_lock);
		log_err(ERR_POOL4_NOT_FOUND, "The address is not part of the pool.");
		return ERR_POOL4_NOT_FOUND;
	}

	destroy_pool_node(node);

	spin_unlock_bh(&pool_lock);
	return ERR_SUCCESS;
}

bool pool4_get_any(u_int8_t l4protocol, __be16 port, struct ipv4_tuple_address *result)
{
	struct pool_node *node;
	__u16 cpu_port;

	spin_lock_bh(&pool_lock);

	if (list_empty(&pool)) {
		spin_unlock(&pool_lock);
		log_err(ERR_POOL4_EMPTY, "The IPv4 pool is empty.");
		return false;
	}

	// Find an address with a compatible port
	cpu_port = be16_to_cpu(port);
	list_for_each_entry(node, &pool, next) {
		if (extract_any_port(get_section(get_ids(node, l4protocol), cpu_port), &result->l4_id)) {
			result->address = node->address;
			spin_unlock_bh(&pool_lock);
			return true;
		}
	}

	// All compatible ports are taken. Go to a corner and cry...
	spin_unlock_bh(&pool_lock);
	return false;
}

bool pool4_get_similar(u_int8_t l4protocol, struct ipv4_tuple_address *address,
		struct ipv4_tuple_address *result)
{
	struct pool_node *node;

	if (!address) {
		log_err(ERR_NULL, "NULL is not a valid address.");
		return false;
	}

	spin_lock_bh(&pool_lock);

	node = get_pool_node(&address->address);
	if (!node) {
		log_err(ERR_POOL4_NOT_FOUND, "%pI4 does not belong to the pool.", &address->address);
		goto failure;
	}

	// TODO (later) el RFC permite usar puerto de diferente paridad/rango si aquí no se encuentra.
	result->address = address->address;
	if (extract_any_port(get_section(get_ids(node, l4protocol), address->l4_id), &result->l4_id)) {
		spin_unlock_bh(&pool_lock);
		return true;
	}

	// Fall through.

failure:
	spin_unlock_bh(&pool_lock);
	return false;
}

bool pool4_return(u_int8_t l4protocol, struct ipv4_tuple_address *address)
{
	struct pool_node *node;
	struct addr_section *section;
	struct free_port *new_port;

	if (!address) {
		log_err(ERR_NULL, "NULL is not a valid address.");
		return false;
	}

	spin_lock_bh(&pool_lock);

	node = get_pool_node(&address->address);
	if (!node) {
		log_err(ERR_POOL4_NOT_FOUND, "%pI4 does not belong to the pool.", &address->address);
		goto failure;
	}
	section = get_section(get_ids(node, l4protocol), address->l4_id);
	if (!section)
		goto failure;

	new_port = kmalloc(sizeof(*new_port), GFP_ATOMIC);
	if (!new_port) {
		// Well, crap. I guess we won't be seeing this address/port anymore :/.
		log_err(ERR_ALLOC_FAILED, "Cannot instantiate! I won't be able to remember that %pI4#%u "
				"can be reused.", &address->address, address->l4_id);
		goto failure;
	}

	new_port->port = address->l4_id;
	list_add(&new_port->next, section->free_ports.prev);

	spin_unlock_bh(&pool_lock);
	return true;

failure:
	spin_unlock_bh(&pool_lock);
	return false;
}

bool pool4_contains(struct in_addr *address)
{
	bool result;

	spin_lock_bh(&pool_lock);
	result = (get_pool_node(address) != NULL);
	spin_unlock_bh(&pool_lock);

	return result;
}

enum error_code pool4_to_array(struct in_addr **array_out, __u32 *size_out)
{
	struct list_head *cursor;
	struct pool_node *node;

	struct in_addr *array;
	__u32 size;

	size = 0;
	spin_lock_bh(&pool_lock);
	list_for_each(cursor, &pool)
		size++;

	array = kmalloc(size * sizeof(*node), GFP_ATOMIC);
	if (!array) {
		spin_unlock_bh(&pool_lock);
		log_err(ERR_ALLOC_FAILED, "Could not allocate the array meant to hold the table.");
		return ERR_ALLOC_FAILED;
	}

	size = 0;
	list_for_each_entry(node, &pool, next) {
		memcpy(&array[size], &node->address, sizeof(struct in_addr));
		size++;
	}
	spin_unlock_bh(&pool_lock);

	*array_out = array;
	*size_out = size;
	return ERR_SUCCESS;
}
