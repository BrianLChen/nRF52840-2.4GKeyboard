/*
 * 单连接、多配对 slot 的管理层。
 *
 * Pairing key 操作当前 slot，再次按下取消；Device Switch key 循环选择全部正式 slot。
 * 所有状态变化、广播和 Flash 提交都在系统工作队列串行执行；蓝牙回调
 * 只投递事件并持有连接引用。recycled 回调及延迟重试处理单连接对象
 * 尚未释放的问题，不能在 disconnected 回调中直接重新广播。
 */
#include <kbd_define.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pairing, KBD_LOG_LEVEL);

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include "pairing.h"
#include "pairing_store.h"

BUILD_ASSERT(CONFIG_BT_MAX_CONN == 1, "Pairing manager supports one connection");
BUILD_ASSERT(CONFIG_BT_MAX_PAIRED >= CONFIG_KBD_MAX_PAIRED + 1,
	     "Reserve one bond for replacement without deleting old keys");
BUILD_ASSERT(CONFIG_BT_ID_MAX >= CONFIG_KBD_MAX_PAIRED + 2,
	     "Reserve default identity and one temporary identity");
/* 临时身份与旧身份必须能同时保留同一电脑的 bond。AUTO_SWAP 只切换
 * 控制器地址解析条目，不改变应用 slot，也不提前删除另一 slot 的密钥。
 * 不可用 UNPAIR_MATCHING_BONDS 替代，否则取消配对时旧数据已不可恢复。
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_BT_ID_AUTO_SWAP_MATCHING_BONDS),
	     "Replacement pairing requires coexistence of matching peer bonds");
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_ID_UNPAIR_MATCHING_BONDS),
	     "The stack must not unpair another slot automatically");

#define INVALID_ID 0xff
#define SLOT_BYTES 9
#define RECORD_BYTES (4 + SLOT_BYTES * CONFIG_KBD_MAX_PAIRED)

static struct slot {
	bool valid;
	uint8_t id;
	bt_addr_le_t peer;
} slots[CONFIG_KBD_MAX_PAIRED];
static uint8_t active_slot;
/* active 只在选择写入成功后改变；pending 可以随连续 Device Switch key 按键变化。
 * pairing_target 和 pairing_old 在配对开始时冻结，绝不随 pending 移动，
 * 防止迟到的成功事件或取消操作误删另一 slot 的正式数据。
 */
static uint8_t pending_slot;
static uint8_t pairing_target = INVALID_ID;
static struct slot pairing_old;
static bool switch_after_cancel;
static bool pair_after_switch;
static void (*release_hid_keys)(struct bt_conn *conn);
static bool record_loaded;
static int record_error;

enum mode { NORMAL, ENTERING, PAIRING, CANCELLING, SWITCHING, POWERING_OFF };
static enum mode mode;
static uint8_t candidate = INVALID_ID;
static struct bt_conn *current_conn;
static bool disconnect_requested;
static atomic_t ready;
static atomic_t advertising;
/* 回调只读取原子量，不跨线程读取工作队列维护的 slot / mode。 */
static atomic_t accepting_id = ATOMIC_INIT(INVALID_ID);
static atomic_t reports_enabled;
static atomic_t queue_fault;
/* disconnected 仅表示链路断开，recycled 才表示唯一连接对象可再次使用。 */
static atomic_t connection_busy;
/* Close the input gate as soon as a scan-thread management request is queued. */
static atomic_t pending_commands;
static atomic_t poweroff_allowed;
static bool poweroff_waiting; /* System workqueue only. */
static int poweroff_result; /* Published to scan thread by k_work_flush(). */
K_SEM_DEFINE(poweroff_done, 0, 1);

enum event_type { BUTTON, NEXT_SLOT, CONNECTED, DISCONNECTED, SUCCESS, FAILED, WAKE };
struct event {
	enum event_type type;
	struct bt_conn *conn;
};
K_MSGQ_DEFINE(events, sizeof(struct event), 16, 4);
static void process_events(struct k_work *work);
static void retry_advertising(struct k_work *work);
static void prepare_poweroff_work(struct k_work *work);
K_WORK_DEFINE(event_work, process_events);
K_WORK_DELAYABLE_DEFINE(retry_work, retry_advertising);
K_WORK_DEFINE(poweroff_work, prepare_poweroff_work);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		CONFIG_BT_DEVICE_APPEARANCE & 0xff, CONFIG_BT_DEVICE_APPEARANCE >> 8),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void print_peer(const char *step, uint8_t id, const bt_addr_le_t *peer)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(peer, addr, sizeof(addr));
	LOG_INF("[pair] %s slot=%u local_id=%u computer=%s\n",
	       step, active_slot + 1, id, addr);
}

static void print_conn(const char *step, struct bt_conn *conn)
{
	struct bt_conn_info info;
	if (!bt_conn_get_info(conn, &info)) {
		print_peer(step, info.id, bt_conn_get_dst(conn));
	}
}

/* 手工序列化：不将 C 结构体的 padding 或 bool 表示写入 Flash。
 * header = version, active_slot, saved_capacity, reserved。
 * 每个 slot = valid, local_id, peer_type, peer_address[6]。
 * 将容量由 2 增加到 3 时可直接读取原有记录，新增 slot 默认为空。
 */
static void encode_slots(uint8_t record[RECORD_BYTES])
{
	memset(record, 0, RECORD_BYTES);
	record[0] = 1;
	record[1] = active_slot;
	record[2] = CONFIG_KBD_MAX_PAIRED;
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		uint8_t *p = &record[4 + i * SLOT_BYTES];
		p[0] = slots[i].valid;
		p[1] = slots[i].id;
		p[2] = slots[i].peer.type;
		memcpy(&p[3], slots[i].peer.a.val, 6);
	}
}

static int load_slots(const char *name, size_t len, settings_read_cb read_cb, void *arg)
{
	uint8_t record[RECORD_BYTES];
	if (strcmp(name, "slots") != 0) {
		return -ENOENT;
	}
	record_loaded = true;
	if (len < 4 || len > sizeof(record) || read_cb(arg, record, len) != (ssize_t)len ||
	    record[0] != 1 || !record[2] || record[2] > CONFIG_KBD_MAX_PAIRED ||
	    record[1] >= record[2] || len != 4 + record[2] * SLOT_BYTES) {
		record_error = -EINVAL;
		return record_error;
	}
	active_slot = record[1];
	for (size_t i = 0; i < record[2]; i++) {
		const uint8_t *p = &record[4 + i * SLOT_BYTES];
		if (p[0] > 1 || (p[0] && (p[1] >= CONFIG_BT_ID_MAX || p[2] > 1))) {
			record_error = -EINVAL;
			return record_error;
		}
		slots[i].valid = p[0];
		slots[i].id = p[1];
		slots[i].peer.type = p[2];
		memcpy(slots[i].peer.a.val, &p[3], 6);
	}
	return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(kbd_slots, "kbd", NULL, load_slots, NULL, NULL);

struct bond_list {
	struct slot entries[CONFIG_BT_MAX_PAIRED];
	size_t count;
	uint8_t id;
};

static void collect_bond(const struct bt_bond_info *info, void *user_data)
{
	struct bond_list *list = user_data;
	if (list->count < ARRAY_SIZE(list->entries)) {
		struct slot *entry = &list->entries[list->count++];
		entry->valid = true;
		entry->id = list->id;
		bt_addr_le_copy(&entry->peer, &info->addr);
	}
}

static void collect_all(struct bond_list *list)
{
	memset(list, 0, sizeof(*list));
	for (size_t i = 0; i < CONFIG_BT_ID_MAX; i++) {
		list->id = i;
		bt_foreach_bond(i, collect_bond, list);
	}
}

static bool same_peer(const struct slot *a, const struct slot *b)
{
	return a->valid && b->valid && a->id == b->id && bt_addr_le_eq(&a->peer, &b->peer);
}

static bool id_in_use(uint8_t id)
{
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (slots[i].valid && slots[i].id == id) {
			return true;
		}
	}
	return false;
}

static bool bond_in_use(const struct slot *bond)
{
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (same_peer(&slots[i], bond)) {
			return true;
		}
	}
	return false;
}

/* bt_id_delete 内部会删除该 identity 的全部 bond，所以所有删除入口
 * 都经过同一个保护函数。共享 identity（包括旧 demo 的 identity 0）
 * 只能按 peer 精确删除 bond，不能批量删除 identity。
 */
static int delete_unused_identity(uint8_t id)
{
	if (id == BT_ID_DEFAULT || id == INVALID_ID || id_in_use(id)) {
		LOG_INF("[pair] REFUSE identity deletion: local_id=%u is reserved/referenced\n", id);
		return -EPERM;
	}
	return bt_id_delete(id);
}

static bool slot_available(uint8_t index)
{
	bt_addr_le_t ids[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(ids);
	struct bond_list list = { .id = slots[index].id };
	if (!slots[index].valid) {
		return true; /* 空 slot 是合法的选择，等待 Pairing key 配对。 */
	}
	bt_id_get(ids, &count);
	if (slots[index].id >= count || bt_addr_le_eq(&ids[slots[index].id], BT_ADDR_LE_ANY)) {
		return false;
	}
	bt_foreach_bond(slots[index].id, collect_bond, &list);
	for (size_t i = 0; i < list.count; i++) {
		if (same_peer(&slots[index], &list.entries[i])) {
			return true;
		}
	}
	return false;
}

static void pause_input(void)
{
	/* 先关掉正常输入，再用专用回调尝试发送全释放报告。该报告不受
	 * reports_enabled 限制；链路不可用时仍清空本地状态，不阻塞切换。
	 */
	if (atomic_set(&reports_enabled, 0) && release_hid_keys) {
		release_hid_keys(current_conn);
	}
}

static void begin_pairing(void)
{
	pause_input();
	pairing_target = active_slot;
	pairing_old = slots[pairing_target];
	LOG_INF("[pair] Enter pairing, frozen target slot=%u\n", pairing_target + 1);
	if (pairing_old.valid) {
		print_peer("keeping OLD computer until commit", pairing_old.id, &pairing_old.peer);
	}
	mode = ENTERING;
}

static void print_slots(void)
{
	size_t count = 0;
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		count += slots[i].valid;
	}
	LOG_INF("[pair] Saved computers: %u/%u, active slot=%u\n",
	       (unsigned int)count, CONFIG_KBD_MAX_PAIRED, active_slot + 1);
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		char addr[BT_ADDR_LE_STR_LEN];
		if (slots[i].valid) {
			bt_addr_le_to_str(&slots[i].peer, addr, sizeof(addr));
			LOG_INF("[pair] slot=%u local_id=%u computer=%s%s\n",
			       (unsigned int)i + 1, slots[i].id, addr,
			       i == active_slot ? " [active]" : "");
		} else {
			LOG_INF("[pair] slot=%u empty\n", (unsigned int)i + 1);
		}
	}
}

static void post_event(enum event_type type, struct bt_conn *conn)
{
	struct event event = { .type = type, .conn = conn ? bt_conn_ref(conn) : NULL };
	if (!atomic_get(&ready)) {
		if (event.conn) {
			bt_conn_unref(event.conn);
		}
		return;
	}
	bool command = type == BUTTON || type == NEXT_SLOT;
	if (command) {
		atomic_inc(&pending_commands);
	}
	if (k_msgq_put(&events, &event, K_NO_WAIT)) {
		if (command) {
			atomic_dec(&pending_commands);
		}
		if (event.conn) {
			bt_conn_unref(event.conn);
		}
		atomic_set(&queue_fault, 1);
		atomic_set(&accepting_id, INVALID_ID);
		LOG_INF("[pair] Event queue overflow; disabling pairing until reboot\n");
	}
	k_work_submit(&event_work);
}

static int stop_advertising(void)
{
	int err = bt_le_adv_stop();
	if (!err || err == -EALREADY) {
		atomic_clear(&advertising);
		return 0;
	}
	LOG_INF("[pair] Stop advertising failed: %d\n", err);
	return err;
}

static void request_disconnect(void)
{
	if (!current_conn || disconnect_requested) {
		return;
	}
	print_conn("disconnect requested", current_conn);
	int err = bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (!err || err == -ENOTCONN) {
		disconnect_requested = true;
	} else {
		LOG_INF("[pair] Disconnect failed: %d; retrying\n", err);
		k_work_reschedule(&retry_work, K_MSEC(250));
	}
}

static int create_candidate(void)
{
	bt_addr_le_t ids[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(ids);
	int id = -ENOMEM;

	bt_id_get(ids, &count);
	pairing_store_begin();
	/* bt_id_create 不会自动填补被删除的中间槽位，必须用 reset 回收。
	 * 永不 reset 正式 slot 的 identity，否则它的旧 bond 会立即被删除。
	 */
	for (size_t i = 1; i < count; i++) {
		if (!id_in_use(i) && bt_addr_le_eq(&ids[i], BT_ADDR_LE_ANY)) {
			id = bt_id_reset(i, NULL, NULL);
			break;
		}
	}
	if (id == -ENOMEM && count < ARRAY_SIZE(ids)) {
		id = bt_id_create(NULL, NULL);
	}
	if (id < 0) {
		pairing_store_discard();
		return id;
	}
	candidate = id;
	count = ARRAY_SIZE(ids);
	bt_id_get(ids, &count);
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&ids[id], addr, sizeof(addr));
	LOG_INF("[pair] Temporary local_id=%d keyboard=%s (RAM only)\n", id, addr);
	return 0;
}

static void start_advertising(void)
{
	if (current_conn || atomic_get(&connection_busy) || atomic_get(&advertising)) {
		return;
	}
	if (mode != PAIRING && (mode != NORMAL || !slots[active_slot].valid)) {
		return;
	}
	struct bt_le_adv_param param = {
		.id = mode == PAIRING ? candidate : slots[active_slot].id,
		.options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
	};
	int err;
	if (mode == NORMAL) {
		/* 普通模式只接受当前电脑连接，避免其他已配对电脑抢占唯一连接。
		 * 配对模式不设置 FILTER_CONN / FILTER_SCAN，所有电脑都能扫描连接。
		 */
		err = bt_le_filter_accept_list_clear();
		if (!err) {
			err = bt_le_filter_accept_list_add(&slots[active_slot].peer);
		}
		if (err) {
			LOG_INF("[pair] Configure reconnect filter failed: %d\n", err);
			k_work_reschedule(&retry_work, K_SECONDS(1));
			return;
		}
		param.options |= BT_LE_ADV_OPT_FILTER_CONN;
	}
	err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		/* disconnected 发生时，协议栈可能还持有连接对象；由 recycled
		 * 或延迟任务重试。不要忙等，也不要在蓝牙 RX 线程阻塞等待。
		 */
		LOG_INF("[pair] Advertising start failed id=%u err=%d; retry\n", param.id, err);
		k_work_reschedule(&retry_work, K_SECONDS(1));
		return;
	}
	atomic_set(&advertising, 1);
	if (mode == PAIRING) {
		LOG_INF("[pair] Pairing advertising ON slot=%u local_id=%u; Pairing key cancels\n",
		       active_slot + 1, candidate);
	} else {
		print_peer("reconnect advertising ON", param.id, &slots[active_slot].peer);
	}
}

static void cancel_candidate(void)
{
	/* 调用时临时连接和广播已经停止。若提交曾部分写入失败，也必须
	 * 将清理操作真正写入后端，而非随 RAM 事务一起丢弃。
	 */
	/* 先验证候选身份归属，再解除 RAM 暂存。异常时保留正式数据，
	 * 停止服务等待重启，不能尝试“修复”一个仍被其他 slot 引用的身份。
	 */
	if (candidate != INVALID_ID &&
	    (candidate == BT_ID_DEFAULT || id_in_use(candidate))) {
		LOG_INF("[pair] REFUSE candidate cleanup: local_id=%u belongs to a slot\n", candidate);
		atomic_clear(&ready);
		return;
	}
	pairing_store_discard();
	if (candidate != INVALID_ID) {
		int err = delete_unused_identity(candidate);
		if (err && err != -EALREADY) {
			LOG_INF("[pair] Candidate cleanup failed id=%u err=%d; stop until reboot\n",
			       candidate, err);
			atomic_clear(&ready);
			return;
		}
		LOG_INF("[pair] Temporary local_id=%u discarded; old slot preserved\n", candidate);
		candidate = INVALID_ID;
	}
	pairing_target = INVALID_ID;
	mode = switch_after_cancel ? SWITCHING : NORMAL;
	switch_after_cancel = false;
	if (mode == NORMAL) {
		atomic_set(&reports_enabled, 1);
		print_slots();
	}
}

static void apply_pending_slot(void)
{
	uint8_t record[RECORD_BYTES];
	int err = 0;
	/* 普通切换只保存 active_slot，不创建、重置、删除任何正式 identity
	 * 或 bond。编码时仅覆盖 header 的选择字段，其余 slot 数据原样保留。
	 */
	if (!slot_available(pending_slot)) {
		err = -ENOENT;
	} else if (pending_slot != active_slot) {
		encode_slots(record);
		record[1] = pending_slot;
		err = settings_save_one("kbd/slots", record, sizeof(record));
	}
	if (err) {
		LOG_INF("[slot] Selection failed target=%u err=%d; restore slot=%u\n",
		       pending_slot + 1, err, active_slot + 1);
		pending_slot = active_slot;
		pair_after_switch = false;
	} else {
		LOG_INF("[slot] Selected %u -> %u (saved)\n", active_slot + 1, pending_slot + 1);
		active_slot = pending_slot;
		print_slots();
		if (!slots[active_slot].valid) {
			LOG_INF("[slot] Empty slot: advertising OFF, Pairing key starts pairing\n");
		}
	}
	mode = NORMAL;
	if (pair_after_switch) {
		pair_after_switch = false;
		begin_pairing();
	} else {
		atomic_set(&reports_enabled, 1);
	}
}

static void drive_state(void)
{
	if (!atomic_get(&ready)) {
		return;
	}
	if (mode == POWERING_OFF) {
		/* Keep processing late CONNECTED / DISCONNECTED / RECYCLED events,
		 * but never restart advertising or create a new pairing transaction.
		 */
		if (stop_advertising()) {
			k_work_reschedule(&retry_work, K_MSEC(250));
			return;
		}
		if (current_conn) {
			request_disconnect();
			return;
		}
		if (atomic_get(&connection_busy)) {
			return;
		}
		if (poweroff_waiting) {
			poweroff_waiting = false;
			atomic_clear(&ready);
			(void)k_work_cancel_delayable(&retry_work);
			k_sem_give(&poweroff_done);
		}
		return;
	}
	/* 一次工作允许 CANCELLING -> SWITCHING -> ENTERING 连续前进，
	 * 但只要存在旧连接就返回，让 DISCONNECTED / RECYCLED 事件继续驱动。
	 */
	while (mode == ENTERING || mode == CANCELLING || mode == SWITCHING) {
		if (stop_advertising()) {
			k_work_reschedule(&retry_work, K_MSEC(250));
			return;
		}
		if (current_conn) {
			request_disconnect();
			return;
		}
		if (atomic_get(&connection_busy)) {
			return;
		}
		if (mode == CANCELLING) {
			cancel_candidate();
			if (!atomic_get(&ready)) {
				return;
			}
		} else if (mode == SWITCHING) {
			apply_pending_slot();
		} else {
			int err = create_candidate();
			if (err) {
				LOG_INF("[pair] Create identity failed: %d; restoring old slot\n", err);
				mode = NORMAL;
				pairing_target = INVALID_ID;
				atomic_set(&reports_enabled, 1);
			} else {
				mode = PAIRING;
				atomic_set(&accepting_id, candidate);
			}
		}
	}
	if (atomic_get(&ready)) {
		start_advertising();
	}
}

static void commit_pairing(struct bt_conn *conn)
{
	struct bt_conn_info info;
	struct bond_list list = { .id = candidate };
	uint8_t record[RECORD_BYTES];
	struct slot old = pairing_old;
	if (mode != PAIRING || conn != current_conn || bt_conn_get_info(conn, &info) ||
	    info.id != candidate) {
		LOG_INF("[pair] Ignore stale pairing success\n");
		return;
	}
	atomic_set(&accepting_id, INVALID_ID);
	/* 成功事件只能提交到进入配对时冻结的 slot；候选 identity 必须独立。
	 * 比较旧快照避免未来扩展代码意外修改目标后仍清理错误的旧设备。
	 */
	if (pairing_target >= ARRAY_SIZE(slots) || active_slot != pairing_target ||
	    candidate == BT_ID_DEFAULT || id_in_use(candidate) ||
	    slots[pairing_target].valid != old.valid ||
	    (old.valid && !same_peer(&slots[pairing_target], &old))) {
		LOG_INF("[pair] Target/identity ownership mismatch; refusing commit and deletion\n");
		mode = CANCELLING;
		return;
	}
	bt_foreach_bond(candidate, collect_bond, &list);
	if (list.count != 1) {
		LOG_INF("[pair] Expected one candidate bond, found %u; rollback\n",
		       (unsigned int)list.count);
		mode = CANCELLING;
		return;
	}
	print_peer("bond succeeded, committing", candidate, &list.entries[0].peer);
	slots[pairing_target] = list.entries[0];
	encode_slots(record);
	int err = pairing_store_commit(record, sizeof(record));
	if (err) {
		slots[pairing_target] = old;
		LOG_INF("[pair] Flash commit failed: %d; keeping OLD computer\n", err);
		mode = CANCELLING;
		return;
	}
	/* slot 提交成功后才允许删除旧 bond。旧 demo 可能有两个电脑共用
	 * identity 0，因此只删除目标 peer，并仅在无其他 slot 引用时删 identity。
	 */
	print_peer("COMMITTED", candidate, &slots[active_slot].peer);
	candidate = INVALID_ID;
	pairing_target = INVALID_ID;
	mode = NORMAL;
	atomic_set(&reports_enabled, 1);
	/* 电脑界面显示配对成功不等于本机持久化成功。只有完成上述提交，
	 * 才明确报告退出 pairing；保持新连接，无需用户再按 Pairing key。
	 */
	print_peer("Pairing mode OFF: committed, NORMAL mode", slots[active_slot].id,
		   &slots[active_slot].peer);
	if (old.valid && !bond_in_use(&old)) {
		print_peer("removing OLD bond", old.id, &old.peer);
		err = bt_unpair(old.id, &old.peer);
		if (!err && old.id != BT_ID_DEFAULT && !id_in_use(old.id)) {
			err = delete_unused_identity(old.id);
		}
		if (err) {
			LOG_INF("[pair] Old data cleanup err=%d; boot recovery will retry\n", err);
		}
	} else if (old.valid) {
		/* 即使两个 slot 异常地引用相同 bond，也宁可保留，绝不误删。 */
		print_peer("OLD bond still referenced by another slot; keeping", old.id, &old.peer);
	}
	print_slots();
}

static void process_events(struct k_work *work)
{
	atomic_clear(&poweroff_allowed);
	struct event event;
	ARG_UNUSED(work);
	while (!k_msgq_get(&events, &event, K_NO_WAIT)) {
		if (atomic_get(&queue_fault)) {
			/* 一旦有事件丢失，连已排队的 SUCCESS 也不能再提交。 */
			if (event.conn) {
				(void)bt_conn_disconnect(event.conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
				bt_conn_unref(event.conn);
			}
			continue;
		}
		switch (event.type) {
		case BUTTON:
			if (mode == POWERING_OFF) {
				break;
			}
			if (mode == NORMAL) {
				begin_pairing();
			} else if (mode == SWITCHING || (mode == CANCELLING && switch_after_cancel)) {
				/* 切换过程中的 Pairing key 延后到最终 pending slot；再按可取消
				 * 这个排队请求。绝不在旧连接未清理时给旧 slot 配对。
				 */
				pair_after_switch = !pair_after_switch;
				LOG_INF("[slot] Pair after switch to slot=%u: %s\n", pending_slot + 1,
				       pair_after_switch ? "YES" : "NO");
			} else if (mode != CANCELLING) {
				LOG_INF("[pair] Pairing key: cancel pairing\n");
				atomic_set(&accepting_id, INVALID_ID);
				mode = CANCELLING;
			}
			break;
		case NEXT_SLOT:
			if (mode == POWERING_OFF) {
				break;
			}
			if (CONFIG_KBD_MAX_PAIRED == 1) {
				LOG_INF("[slot] Only one slot configured; no switch\n");
				break;
			}
			pause_input();
			atomic_set(&accepting_id, INVALID_ID);
			if (mode != SWITCHING && !switch_after_cancel) {
				pending_slot = active_slot;
			}
			pending_slot = (pending_slot + 1) % CONFIG_KBD_MAX_PAIRED;
			LOG_INF("[slot] Device Switch key: active=%u target=%u%s\n", active_slot + 1,
			       pending_slot + 1, slots[pending_slot].valid ? " paired" : " empty");
			if (slots[pending_slot].valid) {
				char addr[BT_ADDR_LE_STR_LEN];
				bt_addr_le_to_str(&slots[pending_slot].peer, addr, sizeof(addr));
				LOG_INF("[slot] Target slot=%u local_id=%u computer=%s\n",
				       pending_slot + 1, slots[pending_slot].id, addr);
			}
			if (mode == NORMAL || mode == SWITCHING) {
				mode = SWITCHING;
			} else {
				/* 先取消当前候选，之后才允许选择别的 slot。active_slot
				 * 在整个取消阶段不变，因此旧配对和调试日志的归属明确。
				 */
				switch_after_cancel = true;
				mode = CANCELLING;
				LOG_INF("[slot] Cancel pairing before switching\n");
			}
			break;
		case CONNECTED:
			atomic_clear(&advertising);
			if (!current_conn) {
				current_conn = bt_conn_ref(event.conn);
				disconnect_requested = false;
			}
			print_conn("connected", event.conn);
			if (mode == PAIRING) {
				struct bt_conn_info info;
				/* 停止旧广播时，控制器可能已经接受了旧电脑的连接。
				 * 即使 CONNECTED 回调迟到，也不能让旧 identity 占住
				 * pairing 模式的唯一连接对象。
				 */
				if (bt_conn_get_info(event.conn, &info) || info.id != candidate) {
					print_conn("late OLD identity connection, rejecting", event.conn);
					request_disconnect();
				}
			} else if (mode == NORMAL) {
				struct bt_conn_info info;
				if (!slots[active_slot].valid || bt_conn_get_info(event.conn, &info) ||
				    info.id != slots[active_slot].id ||
				    !bt_addr_le_eq(bt_conn_get_dst(event.conn), &slots[active_slot].peer)) {
					print_conn("unexpected computer, rejecting", event.conn);
					request_disconnect();
				} else {
					/* 只对通过 identity + peer 双重检查的连接开放输入。
					 * can_send 还会等待该连接完成加密。
					 */
					atomic_set(&reports_enabled, 1);
				}
			}
			break;
		case DISCONNECTED:
			print_conn("disconnected", event.conn);
			if (current_conn == event.conn) {
				bt_conn_unref(current_conn);
				current_conn = NULL;
				disconnect_requested = false;
			}
			break;
		case SUCCESS:
			commit_pairing(event.conn);
			break;
		case FAILED:
			print_conn("pairing failed / not bonded", event.conn);
			if (current_conn == event.conn) {
				/* 失败后断开这个连接，继续用本次临时 identity 等待重试。 */
				if (mode == PAIRING) {
					LOG_INF("[pair] Local bonding did NOT complete; pairing mode remains ON, "
					       "old slot unchanged\n");
				}
				request_disconnect();
			}
			break;
		case WAKE:
			break;
		}
		if (event.type == BUTTON || event.type == NEXT_SLOT) {
			atomic_dec(&pending_commands);
		}
		if (event.conn) {
			bt_conn_unref(event.conn);
		}
	}
	if (atomic_get(&queue_fault)) {
		/* 丢失事件时无法保证事务顺序，保守停止，不提交也不删除旧配对。 */
		atomic_clear(&ready);
		atomic_clear(&reports_enabled);
		atomic_set(&accepting_id, INVALID_ID);
		(void)stop_advertising();
		request_disconnect();
		return;
	}
	drive_state();
	atomic_set(&poweroff_allowed, atomic_get(&ready) && mode == NORMAL &&
		   candidate == INVALID_ID && !atomic_get(&queue_fault));
}

static void prepare_poweroff_work(struct k_work *work)
{
	ARG_UNUSED(work);
	/* The same workqueue owns mode and all slot commits. Check here, not
	 * just in the scan thread, so shutdown cannot interrupt a transaction.
	 */
	if (!atomic_get(&ready) || atomic_get(&queue_fault) ||
	    atomic_get(&pending_commands) || mode != NORMAL || candidate != INVALID_ID) {
		poweroff_result = -EBUSY;
		return;
	}
	atomic_clear(&poweroff_allowed);
	mode = POWERING_OFF;
	atomic_set(&accepting_id, INVALID_ID);
	poweroff_waiting = true;
	poweroff_result = 0;
	pause_input();
	drive_state();
}

bool kbd_pairing_can_poweroff(void)
{
	return atomic_get(&poweroff_allowed) && atomic_get(&ready) &&
		!atomic_get(&pending_commands) && !atomic_get(&queue_fault);
}

int kbd_pairing_prepare_poweroff(void)
{
	struct k_work_sync sync;

	/* Single caller: the scan thread. Do not time out while a preceding
	 * work item may still be committing settings; first serialize admission.
	 */
	k_sem_reset(&poweroff_done);
	int err = k_work_submit(&poweroff_work);
	if (err < 0) {
		return err;
	}
	(void)k_work_flush(&poweroff_work, &sync);
	if (poweroff_result) {
		return poweroff_result;
	}
	err = k_sem_take(&poweroff_done, K_SECONDS(3));
	if (err) {
		return -ETIMEDOUT;
	}
	(void)k_work_cancel_delayable_sync(&retry_work, &sync);
	(void)k_work_flush(&event_work, &sync);
	return 0;
}

static void retry_advertising(struct k_work *work)
{
	ARG_UNUSED(work);
	post_event(WAKE, NULL);
}

int kbd_pairing_init(void (*release_keys)(struct bt_conn *conn))
{
	struct bond_list list;
	bt_addr_le_t ids[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(ids);
	uint8_t record[RECORD_BYTES];
	int err;

	if (record_error) {
		LOG_INF("[pair] Invalid slot settings; old bonds untouched\n");
		return record_error;
	}
	collect_all(&list);
	bt_id_get(ids, &count);
	if (!record_loaded) {
		/* 首次升级：接纳 demo 已有配对，包括共享默认 identity 的两个 peer。
		 * 在第一次创建临时身份之前先持久化基线（即使一个 bond 都没有），
		 * 避免提交中断后把临时 bond 当作旧版数据重新导入。
		 */
		if (list.count > ARRAY_SIZE(slots)) {
			LOG_INF("[pair] Too many legacy bonds; refusing destructive migration\n");
			return -ENOSPC;
		}
		for (size_t i = 0; i < list.count; i++) {
			slots[i] = list.entries[i];
		}
		encode_slots(record);
		err = settings_save_one("kbd/slots", record, sizeof(record));
		if (err) {
			return err;
		}
		LOG_INF("[pair] Imported %u legacy computers\n", (unsigned int)list.count);
	}
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (!slots[i].valid) {
			continue;
		}
		bool found = false;
		for (size_t j = 0; j < list.count; j++) {
			found |= same_peer(&slots[i], &list.entries[j]);
		}
		if (!found || slots[i].id >= count ||
		    bt_addr_le_eq(&ids[slots[i].id], BT_ADDR_LE_ANY)) {
			LOG_INF("[pair] slot=%u missing identity/bond; stop to preserve data\n",
			       (unsigned int)i + 1);
			return -EINVAL;
		}
	}
	/* 重启恢复：只有已提交的 slot 是权威数据；其余 bond 是提交前断电
	 * 留下的候选，或提交后断电留下的旧 bond。收集后再删除，避免在
	 * bt_foreach_bond 的迭代回调内修改底层密钥池。
	 */
	for (size_t j = 0; j < list.count; j++) {
		bool keep = false;
		for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
			keep |= same_peer(&slots[i], &list.entries[j]);
		}
		if (!keep) {
			print_peer("boot cleanup orphan bond", list.entries[j].id,
				   &list.entries[j].peer);
			err = bt_unpair(list.entries[j].id, &list.entries[j].peer);
			if (err) {
				return err;
			}
		}
	}
	for (size_t i = count; i > 1; i--) {
		uint8_t id = i - 1;
		if (!id_in_use(id) && !bt_addr_le_eq(&ids[id], BT_ADDR_LE_ANY)) {
			err = delete_unused_identity(id);
			if (err) {
				return err;
			}
			LOG_INF("[pair] Boot cleanup unused local_id=%u\n", id);
		}
	}
	print_slots();
	pending_slot = active_slot;
	release_hid_keys = release_keys;
	LOG_INF("[pair] Pairing key: enter/cancel pairing. Device Switch key: select next slot (including empty).\n");
	if (!slots[active_slot].valid) {
		LOG_INF("[pair] Active slot empty; press Pairing key to pair\n");
	}
	atomic_set(&reports_enabled, 1);
	atomic_set(&ready, 1);
	post_event(WAKE, NULL);
	return 0;
}

void kbd_pairing_button(void) { post_event(BUTTON, NULL); }
void kbd_pairing_next_slot(void) { post_event(NEXT_SLOT, NULL); }
void kbd_pairing_connected(struct bt_conn *conn, uint8_t err)
{
	atomic_clear(&advertising);
	if (!err) {
		atomic_clear(&reports_enabled);
		atomic_set(&connection_busy, 1);
	}
	post_event(err ? WAKE : CONNECTED, err ? NULL : conn);
}
void kbd_pairing_disconnected(struct bt_conn *conn) { post_event(DISCONNECTED, conn); }
void kbd_pairing_recycled(void)
{
	atomic_clear(&connection_busy);
	post_event(WAKE, NULL);
}
void kbd_pairing_complete(struct bt_conn *conn, bool bonded)
{
	struct bt_conn_info info;
	if (bonded && !bt_conn_get_info(conn, &info) && info.id == atomic_get(&accepting_id)) {
		/* 在 RX 回调中立即关掉新 SMP 请求，避免 Flash 提交排队期间
		 * 对端再次配对、更换刚刚生成的候选密钥。
		 */
		atomic_set(&accepting_id, INVALID_ID);
	}
	post_event(bonded ? SUCCESS : FAILED, conn);
}
void kbd_pairing_failed(struct bt_conn *conn) { post_event(FAILED, conn); }

bool kbd_pairing_accept(struct bt_conn *conn)
{
	struct bt_conn_info info;
	bool allow = atomic_get(&ready) && !bt_conn_get_info(conn, &info) &&
		info.id == atomic_get(&accepting_id);
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("[pair] SMP request computer=%s: %s\n", addr, allow ? "ACCEPT" : "REJECT");
	return allow;
}

bool kbd_pairing_is_advertising(void) { return atomic_get(&advertising); }
bool kbd_pairing_can_send(struct bt_conn *conn)
{
	return atomic_get(&ready) && !atomic_get(&queue_fault) &&
		!atomic_get(&pending_commands) && atomic_get(&reports_enabled) &&
		bt_conn_get_security(conn) >= BT_SECURITY_L2;
}
