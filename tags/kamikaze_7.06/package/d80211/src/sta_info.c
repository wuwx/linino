/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>

#include <net/d80211.h>
#include "ieee80211_i.h"
#include "ieee80211_rate.h"
#include "sta_info.h"


/* Caller must hold local->sta_lock */
static void sta_info_hash_add(struct ieee80211_local *local,
			      struct sta_info *sta)
{
	sta->hnext = local->sta_hash[STA_HASH(sta->addr)];
	local->sta_hash[STA_HASH(sta->addr)] = sta;
}


/* Caller must hold local->sta_lock */
static void sta_info_hash_del(struct ieee80211_local *local,
			      struct sta_info *sta)
{
	struct sta_info *s;

	s = local->sta_hash[STA_HASH(sta->addr)];
	if (!s)
		return;
	if (memcmp(s->addr, sta->addr, ETH_ALEN) == 0) {
		local->sta_hash[STA_HASH(sta->addr)] = s->hnext;
		return;
	}

	while (s->hnext && memcmp(s->hnext->addr, sta->addr, ETH_ALEN) != 0)
		s = s->hnext;
	if (s->hnext)
		s->hnext = s->hnext->hnext;
	else
		printk(KERN_ERR "%s: could not remove STA " MAC_FMT " from "
		       "hash table\n", local->mdev->name, MAC_ARG(sta->addr));
}

static inline struct sta_info *__sta_info_get(struct sta_info *sta)
{
	return kobject_get(&sta->kobj) ? sta : NULL;
}

struct sta_info * sta_info_get(struct ieee80211_local *local, u8 *addr)
{
	struct sta_info *sta;

	spin_lock_bh(&local->sta_lock);
	sta = local->sta_hash[STA_HASH(addr)];
	while (sta) {
		if (memcmp(sta->addr, addr, ETH_ALEN) == 0) {
			__sta_info_get(sta);
			break;
		}
		sta = sta->hnext;
	}
	spin_unlock_bh(&local->sta_lock);

	return sta;
}
EXPORT_SYMBOL(sta_info_get);

int sta_info_min_txrate_get(struct ieee80211_local *local)
{
	struct sta_info *sta;
        int min_txrate = 9999999;
        int i;

	spin_lock_bh(&local->sta_lock);
	for (i = 0; i < STA_HASH_SIZE; i++) {
		sta = local->sta_hash[i];
		while (sta) {
			if (sta->txrate < min_txrate)
				min_txrate = sta->txrate;
			sta = sta->hnext;
		}
	}
	spin_unlock_bh(&local->sta_lock);
	if (min_txrate == 9999999)
		min_txrate = 0;

	return min_txrate;
}


void sta_info_put(struct sta_info *sta)
{
	kobject_put(&sta->kobj);
}
EXPORT_SYMBOL(sta_info_put);

void sta_info_release(struct kobject *kobj)
{
	struct sta_info *sta = container_of(kobj, struct sta_info, kobj);
	struct ieee80211_local *local = sta->local;
	struct sk_buff *skb;

	/* free sta structure; it has already been removed from
	 * hash table etc. external structures. Make sure that all
	 * buffered frames are release (one might have been added
	 * after sta_info_free() was called). */
	while ((skb = skb_dequeue(&sta->ps_tx_buf)) != NULL) {
		local->total_ps_buffered--;
		dev_kfree_skb_any(skb);
	}
	while ((skb = skb_dequeue(&sta->tx_filtered)) != NULL) {
		dev_kfree_skb_any(skb);
	}
	rate_control_free_sta(sta->rate_ctrl, sta->rate_ctrl_priv);
	rate_control_put(sta->rate_ctrl);
	kfree(sta);
}


struct sta_info * sta_info_add(struct ieee80211_local *local,
			       struct net_device *dev, u8 *addr, gfp_t gfp)
{
	struct sta_info *sta;

	sta = kzalloc(sizeof(*sta), gfp);
	if (!sta)
		return NULL;

	if (kobject_set_name(&sta->kobj, MAC_FMT, MAC_ARG(addr))) {
		kfree(sta);
		return NULL;
	}
	sta->kobj.kset = &local->sta_kset;
	kobject_init(&sta->kobj);

	sta->rate_ctrl = rate_control_get(local->rate_ctrl);
	sta->rate_ctrl_priv = rate_control_alloc_sta(sta->rate_ctrl, gfp);
	if (!sta->rate_ctrl_priv) {
		rate_control_put(sta->rate_ctrl);
		kobject_put(&sta->kobj);
		kfree(sta);
		return NULL;
	}

        memcpy(sta->addr, addr, ETH_ALEN);
	sta->local = local;
        sta->dev = dev;
	skb_queue_head_init(&sta->ps_tx_buf);
	skb_queue_head_init(&sta->tx_filtered);
	__sta_info_get(sta);	/* sta used by caller, decremented by
				 * sta_info_put() */
	spin_lock_bh(&local->sta_lock);
	list_add(&sta->list, &local->sta_list);
	local->num_sta++;
        sta_info_hash_add(local, sta);
	spin_unlock_bh(&local->sta_lock);
	if (local->ops->sta_table_notification)
		local->ops->sta_table_notification(local_to_hw(local),
						  local->num_sta);
	sta->key_idx_compression = HW_KEY_IDX_INVALID;

#ifdef CONFIG_D80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: Added STA " MAC_FMT "\n",
	       local->mdev->name, MAC_ARG(addr));
#endif /* CONFIG_D80211_VERBOSE_DEBUG */

	if (!in_interrupt()) {
		sta->sysfs_registered = 1;
		ieee80211_sta_sysfs_add(sta);
		rate_control_add_sta_attrs(sta, &sta->kobj);
	} else {
		/* procfs entry adding might sleep, so schedule process context
		 * task for adding proc entry for STAs that do not yet have
		 * one. */
		schedule_work(&local->sta_proc_add);
	}

	return sta;
}

static void finish_sta_info_free(struct ieee80211_local *local,
				 struct sta_info *sta)
{
#ifdef CONFIG_D80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: Removed STA " MAC_FMT "\n",
	       local->mdev->name, MAC_ARG(sta->addr));
#endif /* CONFIG_D80211_VERBOSE_DEBUG */

	if (sta->key) {
		ieee80211_key_sysfs_remove(sta->key);
		ieee80211_key_free(sta->key);
		sta->key = NULL;
	}

	rate_control_remove_sta_attrs(sta, &sta->kobj);
	ieee80211_sta_sysfs_remove(sta);

	sta_info_put(sta);
}

void sta_info_free(struct sta_info *sta, int locked)
{
	struct sk_buff *skb;
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata;

	if (!locked)
		spin_lock_bh(&local->sta_lock);
	sta_info_hash_del(local, sta);
	list_del(&sta->list);
	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);
	if (sta->flags & WLAN_STA_PS) {
		sta->flags &= ~WLAN_STA_PS;
		if (sdata->bss)
			atomic_dec(&sdata->bss->num_sta_ps);
	}
	local->num_sta--;
	sta_info_remove_aid_ptr(sta);
	if (!locked)
		spin_unlock_bh(&local->sta_lock);
	if (local->ops->sta_table_notification)
		local->ops->sta_table_notification(local_to_hw(local),
						  local->num_sta);

	while ((skb = skb_dequeue(&sta->ps_tx_buf)) != NULL) {
		local->total_ps_buffered--;
		dev_kfree_skb_any(skb);
	}
	while ((skb = skb_dequeue(&sta->tx_filtered)) != NULL) {
		dev_kfree_skb_any(skb);
	}

	if (sta->key) {
		if (local->ops->set_key) {
			struct ieee80211_key_conf *key;
			key = ieee80211_key_data2conf(local, sta->key);
			if (key) {
				local->ops->set_key(local_to_hw(local),
						   DISABLE_KEY,
						   sta->addr, key, sta->aid);
				kfree(key);
			}
		}
	} else if (sta->key_idx_compression != HW_KEY_IDX_INVALID) {
		struct ieee80211_key_conf conf;
		memset(&conf, 0, sizeof(conf));
		conf.hw_key_idx = sta->key_idx_compression;
		conf.alg = ALG_NULL;
		conf.flags |= IEEE80211_KEY_FORCE_SW_ENCRYPT;
		local->ops->set_key(local_to_hw(local), DISABLE_KEY,
				   sta->addr, &conf, sta->aid);
		sta->key_idx_compression = HW_KEY_IDX_INVALID;
	}

	if (in_atomic()) {
		list_add(&sta->list, &local->deleted_sta_list);
		schedule_work(&local->sta_proc_add);
	} else
		finish_sta_info_free(local, sta);
}


static inline int sta_info_buffer_expired(struct ieee80211_local *local,
					  struct sta_info *sta,
					  struct sk_buff *skb)
{
        struct ieee80211_tx_packet_data *pkt_data;
	int timeout;

	if (!skb)
		return 0;

        pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;

	/* Timeout: (2 * listen_interval * beacon_int * 1024 / 1000000) sec */
	timeout = (sta->listen_interval * local->hw.conf.beacon_int * 32 /
		   15625) * HZ;
	if (timeout < STA_TX_BUFFER_EXPIRE)
		timeout = STA_TX_BUFFER_EXPIRE;
	return time_after(jiffies, pkt_data->jiffies + timeout);
}


static void sta_info_cleanup_expire_buffered(struct ieee80211_local *local,
					     struct sta_info *sta)
{
	unsigned long flags;
	struct sk_buff *skb;

	if (skb_queue_empty(&sta->ps_tx_buf))
		return;

	for (;;) {
		spin_lock_irqsave(&sta->ps_tx_buf.lock, flags);
		skb = skb_peek(&sta->ps_tx_buf);
		if (sta_info_buffer_expired(local, sta, skb)) {
			skb = __skb_dequeue(&sta->ps_tx_buf);
			if (skb_queue_empty(&sta->ps_tx_buf))
				sta->flags &= ~WLAN_STA_TIM;
		} else
			skb = NULL;
		spin_unlock_irqrestore(&sta->ps_tx_buf.lock, flags);

		if (skb) {
			local->total_ps_buffered--;
			printk(KERN_DEBUG "Buffered frame expired (STA "
			       MAC_FMT ")\n", MAC_ARG(sta->addr));
			dev_kfree_skb(skb);
		} else
			break;
	}
}


static void sta_info_cleanup(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sta_info *sta;

	spin_lock_bh(&local->sta_lock);
	list_for_each_entry(sta, &local->sta_list, list) {
		__sta_info_get(sta);
		sta_info_cleanup_expire_buffered(local, sta);
		sta_info_put(sta);
	}
	spin_unlock_bh(&local->sta_lock);

	local->sta_cleanup.expires = jiffies + STA_INFO_CLEANUP_INTERVAL;
	add_timer(&local->sta_cleanup);
}


static void sta_info_proc_add_task(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, sta_proc_add);
	struct sta_info *sta, *tmp;

	while (1) {
		spin_lock_bh(&local->sta_lock);
		if (!list_empty(&local->deleted_sta_list)) {
			sta = list_entry(local->deleted_sta_list.next,
					 struct sta_info, list);
			list_del(local->deleted_sta_list.next);
		} else
			sta = NULL;
		spin_unlock_bh(&local->sta_lock);
		if (!sta)
			break;
		finish_sta_info_free(local, sta);
	}

	while (1) {
		sta = NULL;
		spin_lock_bh(&local->sta_lock);
		list_for_each_entry(tmp, &local->sta_list, list) {
			if (!tmp->sysfs_registered) {
				sta = tmp;
				__sta_info_get(sta);
				break;
			}
		}
		spin_unlock_bh(&local->sta_lock);

		if (!sta)
			break;

		sta->sysfs_registered = 1;
		ieee80211_sta_sysfs_add(sta);
		rate_control_add_sta_attrs(sta, &sta->kobj);
		sta_info_put(sta);
	}
}


void sta_info_init(struct ieee80211_local *local)
{
	spin_lock_init(&local->sta_lock);
	INIT_LIST_HEAD(&local->sta_list);
	INIT_LIST_HEAD(&local->deleted_sta_list);

	init_timer(&local->sta_cleanup);
	local->sta_cleanup.expires = jiffies + STA_INFO_CLEANUP_INTERVAL;
	local->sta_cleanup.data = (unsigned long) local;
	local->sta_cleanup.function = sta_info_cleanup;

	INIT_WORK(&local->sta_proc_add, sta_info_proc_add_task);
}

int sta_info_start(struct ieee80211_local *local)
{
	int res;

	res = ieee80211_sta_kset_sysfs_register(local);
	if (res)
		return res;
	add_timer(&local->sta_cleanup);
	return 0;
}

void sta_info_stop(struct ieee80211_local *local)
{
	struct sta_info *sta, *tmp;

	del_timer(&local->sta_cleanup);

	list_for_each_entry_safe(sta, tmp, &local->sta_list, list) {
		/* sta_info_free must be called with 0 as the last
		 * parameter to ensure all sysfs sta entries are
		 * unregistered. We don't need locking at this
		 * point. */
		sta_info_free(sta, 0);
	}
	ieee80211_sta_kset_sysfs_unregister(local);
}


void sta_info_remove_aid_ptr(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata;

	if (sta->aid <= 0)
		return;

	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);

	if (sdata->local->ops->set_tim)
		sdata->local->ops->set_tim(local_to_hw(sdata->local),
					  sta->aid, 0);
	if (sdata->bss)
		__bss_tim_clear(sdata->bss, sta->aid);
}


/**
 * sta_info_flush - flush matching STA entries from the STA table
 * @local: local interface data
 * @dev: matching rule for the net device (sta->dev) or %NULL to match all STAs
 */
void sta_info_flush(struct ieee80211_local *local, struct net_device *dev)
{
	struct sta_info *sta, *tmp;

	spin_lock_bh(&local->sta_lock);
	list_for_each_entry_safe(sta, tmp, &local->sta_list, list)
		if (!dev || dev == sta->dev)
			sta_info_free(sta, 1);
	spin_unlock_bh(&local->sta_lock);
}
