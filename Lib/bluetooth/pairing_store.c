/*
 * NCS 3.4.0 会在 bt_id_create() 后异步保存 bt/id，并在通知应用
 * pairing_complete() 前保存 bt/keys。普通 settings handler 无法阻止这些写入。
 *
 * 本模块通过链接器 --wrap 拦截 settings_save_one/settings_delete，将配对
 * 事务期间的 bt/* 写入（包括删除）合并到 RAM；其他 settings 正常写入。
 * 两个入口都必须拦截：settings_delete() 在 settings_store.c 内部调用
 * settings_save_one()，链接器不能保证同一目标文件内部调用也被 --wrap。
 *
 * 本项目使用 NVS，未开启 LTO。升级 SDK / 改用 LTO / 新增 settings 写入
 * API 时，需重新确认所有 Bluetooth 写入仍经过上述入口。
 */
#include <kbd_define.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pairing_store, KBD_LOG_LEVEL);

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/util.h>
#include "pairing_store.h"

/* 一个连接的 keys、CCC、GATT 缓存及全局身份表均有独立记录。
 * 容量不足时记录错误，整个事务禁止提交，绝不悄悄丢弃密钥。
 */
#define RECORD_COUNT (8 + 8 * CONFIG_KBD_MAX_PAIRED)
#define VALUE_SIZE MAX(512, CONFIG_BT_ID_MAX * sizeof(bt_addr_le_t))
#define NAME_SIZE 96

static struct staged_record {
	char name[NAME_SIZE];
	size_t len;
	uint8_t value[VALUE_SIZE];
} staged[RECORD_COUNT];
static size_t used;
static bool staging;
static int staging_error;
K_MUTEX_DEFINE(store_mutex);

/* GNU ld --wrap 的真实入口；只用于本模块写入实际的 NVS 后端。 */
int __real_settings_save_one(const char *name, const void *value, size_t len);
int __wrap_settings_save_one(const char *name, const void *value, size_t len);
int __wrap_settings_delete(const char *name);

static int store_value(const char *name, const void *value, size_t len)
{
	int err = 0;

	k_mutex_lock(&store_mutex, K_FOREVER);
	if (!staging || strncmp(name, "bt/", 3) != 0) {
		err = __real_settings_save_one(name, value, len);
		goto out;
	}
	if (strlen(name) >= NAME_SIZE || len > VALUE_SIZE) {
		err = -E2BIG;
		goto failed;
	}
	size_t i;
	for (i = 0; i < used; i++) {
		if (strcmp(staged[i].name, name) == 0) {
			break;
		}
	}
	if (i == RECORD_COUNT) {
		err = -ENOMEM;
		goto failed;
	}
	if (i == used) {
		used++;
		strcpy(staged[i].name, name);
	}
	staged[i].len = len;
	if (len) {
		memcpy(staged[i].value, value, len);
	}
	goto out;
failed:
	staging_error = err;
	LOG_INF("[pair/store] RAM staging failed: %s err=%d\n", name, err);
out:
	k_mutex_unlock(&store_mutex);
	return err;
}

int __wrap_settings_save_one(const char *name, const void *value, size_t len)
{
	return store_value(name, value, len);
}

int __wrap_settings_delete(const char *name)
{
	return store_value(name, NULL, 0);
}

void pairing_store_begin(void)
{
	k_mutex_lock(&store_mutex, K_FOREVER);
	used = 0;
	staging_error = 0;
	staging = true;
	k_mutex_unlock(&store_mutex);
}

void pairing_store_discard(void)
{
	k_mutex_lock(&store_mutex, K_FOREVER);
	staging = false;
	used = 0;
	staging_error = 0;
	/* 不在日志中输出密钥，释放暂存内容时也清除其内存副本。 */
	memset(staged, 0, sizeof(staged));
	k_mutex_unlock(&store_mutex);
}

int pairing_store_commit(const void *slot_record, size_t len)
{
	int err;
	bool has_keys = false;
	bt_addr_le_t ids[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(ids);

	/* 当前实现明确针对无本地 RPA 的配置。若以后开启 privacy，必须同样
	 * 同步保存最新 IRK，不能假设异步 store_irk_work 已经执行。
	 */
	BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_PRIVACY), "Pairing transaction requires BT_PRIVACY=n");
	BUILD_ASSERT(!IS_ENABLED(CONFIG_LTO), "Settings interception requires LTO=n");
	bt_id_get(ids, &count);
	k_mutex_lock(&store_mutex, K_FOREVER);
	err = staging_error;
	if (err || !staging) {
		err = err ? err : -EINVAL;
		goto out;
	}
	for (size_t i = 0; i < used; i++) {
		if (strncmp(staged[i].name, "bt/keys/", 8) == 0 && staged[i].len) {
			has_keys = true;
		}
	}
	if (!has_keys) {
		/* SMP 成功但没捕获到持久化密钥，不能提交一个重启后无法恢复的 slot。 */
		err = -ENODATA;
		goto out;
	}

	/* bt_id_create 的保存任务可能还在系统队列中，主动保存当前身份表。
	 * 保存旧身份和新身份的并集，使提交过程任何时刻掉电都仍有旧身份。
	 */
	err = __real_settings_save_one("bt/id", ids, count * sizeof(ids[0]));
	for (size_t i = 0; !err && i < used; i++) {
		if (strcmp(staged[i].name, "bt/id") == 0) {
			continue;
		}
		err = __real_settings_save_one(staged[i].name,
			staged[i].len ? staged[i].value : NULL, staged[i].len);
	}
	if (!err) {
		/* 单条 NVS 记录是应用的提交点。只有新身份和密钥写入成功后，
		 * 才把 slot 指向新设备；之前任何错误都保留原来的 slot 映射。
		 */
		err = __real_settings_save_one("kbd/slots", slot_record, len);
	}
	if (!err) {
		staging = false;
		used = 0;
		memset(staged, 0, sizeof(staged));
	}
out:
	k_mutex_unlock(&store_mutex);
	return err;
}
