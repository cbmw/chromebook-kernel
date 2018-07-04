/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2016 - 2017        Intel Deutschland GmbH
 * Copyright(c) 2018               Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2016 - 2017        Intel Deutschland GmbH
 * Copyright(c) 2018               Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef __IWL_FMAC_H__
#define __IWL_FMAC_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/leds.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <net/cfg80211.h>

#ifdef CONFIG_THERMAL
#include <linux/thermal.h>
#endif

#include "iwl-op-mode.h"
#include "iwl-trans.h"
#include "fw/notif-wait.h"
#include "fw-api.h"
#include "fw/runtime.h"
#include "fw/dbg.h"
#include "iwl-nvm-parse.h"

extern const u8 tid_to_ac[];

#define IWL_FMAC_RESERVED_TID	4
#define IWL_FMAC_INVALID_TXQ_ID	0xffff
struct iwl_fmac_tid {
	struct sk_buff_head deferred_tx_frames;
	u16 seq_number;
	u16 next_reclaimed;
	u16 txq_id;
};

/**
 * struct iwl_fmac_reorder_buffer - per ra/tid/queue reorder buffer
 * @head_sn: reorder window head sn
 * @num_stored: number of mpdus stored in the buffer
 * @buf_size: the reorder buffer size as set by the last addba request
 * @sta_id: sta id of this reorder buffer
 * @queue: queue of this reorder buffer
 * @last_amsdu: track last ASMDU SN for duplication detection
 * @last_sub_index: track ASMDU sub frame index for duplication detection
 * @entries: list of skbs stored
 * @reorder_time: time the packet was stored in the reorder buffer
 * @reorder_timer: timer for frames are in the reorder buffer. For AMSDU
 *	it is the time of last received sub-frame
 * @lock: protect reorder buffer internal state
 * @fmac: fmac pointer, needed for frame timer context
 */
struct iwl_fmac_reorder_buffer {
	struct iwl_fmac *fmac;
	u16 head_sn;
	u16 num_stored;
	u8 buf_size;
	u8 sta_id;
	int queue;
	u16 last_amsdu;
	u8 last_sub_index;
	struct sk_buff_head entries[IEEE80211_MAX_AMPDU_BUF];
	unsigned long reorder_time[IEEE80211_MAX_AMPDU_BUF];
	struct timer_list reorder_timer;
	spinlock_t lock; /* protect reorder buffer internal state */
} ____cacheline_aligned_in_smp;

/**
 * struct iwl_fmac_rxq_dup_data - per station per rx queue data
 * @last_seq: last sequence per tid for duplicate packet detection
 * @last_sub_frame: last subframe packet
 */
struct iwl_fmac_rxq_dup_data {
	__le16 last_seq[IWL_MAX_TID_COUNT + 1];
	u8 last_sub_frame[IWL_MAX_TID_COUNT + 1];
} ____cacheline_aligned_in_smp;

struct iwl_fmac_tkip_tsc {
	u32 iv32;
	u16 iv16;
};

struct iwl_fmac_sta_key {
	struct rcu_head rcu_head;
	u32 cipher;
	u8 keyidx;
	u8 hw_keyidx;
	u8 iv_len;
	atomic64_t tx_pn;
	union {
		u8 pn[IWL_MAX_TID_COUNT][IEEE80211_CCMP_PN_LEN];
		struct iwl_fmac_tkip_tsc tsc[IWL_MAX_TID_COUNT];
	} ____cacheline_aligned_in_smp q[];
};

struct iwl_fmac_tx_stats {
	/* RATE_MCS_* */
	int last_rate;
	u64 bytes;
	u64 packets;
	u32 retries;
	u32 failed;
};

struct iwl_fmac_rx_stats {
	int last_rate; /* RATE_MCS_* */
	unsigned long last_rx;
	int signal;
	u32 packets;
};

struct iwl_fmac_sta_info {
	struct iwl_fmac_tx_stats tx_stats;
	struct iwl_fmac_rx_stats __percpu *pcpu_rx_stats;
	u32 connect_time;
};

#define IWL_FMAC_INVALID_STA_ID	0xff
#define IWL_FMAC_MAX_STA 16
#define IWL_MAX_BAID	32
#define IWL_FMAC_NON_QOS_TID IWL_MAX_TID_COUNT
struct iwl_fmac_sta {
	u8 addr[ETH_ALEN];
	u8 sta_id;
	u8 ptk_idx; /* default ptk index */
	u8 gtk_idx; /* default gtk index */
	u16 amsdu_enabled;
	u16 amsdu_size;
	bool qos;
	bool encryption;
	bool authorized;
	bool he;
	enum nl80211_band band;
	struct iwl_fmac_vif *vif;
	struct iwl_fmac_rxq_dup_data *dup_data;
	struct iwl_fmac_sta_key __rcu *ptk[UMAC_DEFAULT_KEYS];
	struct iwl_fmac_sta_key __rcu *gtk[UMAC_DEFAULT_KEYS];
	struct iwl_fmac_tid tids[IWL_MAX_TID_COUNT];
	struct iwl_fmac_sta_info info;

	/* indication bitmap of deferred traffic per-TID */
	u16 deferred_traffic_tid_map;

	spinlock_t lock; /* To protect operations on the STA */
#ifdef CPTCFG_CFG80211_DEBUGFS
	struct dentry *dbgfs_dir;
#endif
};

#define for_each_valid_sta(_fmac, _sta, _tmp)				\
	for (_tmp = 0; _tmp < ARRAY_SIZE((_fmac)->stas); _tmp++)	\
		if (((_sta) = rcu_dereference_check((_fmac)->stas[_tmp],\
				    lockdep_is_held(&(_fmac)->mutex))))

enum iwl_fmac_status {
	IWL_STATUS_DUMPING_FW_LOG,
	IWL_STATUS_HW_RFKILL,
	IWL_STATUS_HW_CTKILL,
};

struct iwl_fmac_qos_map {
	struct cfg80211_qos_map qos_map;
	struct rcu_head rcu_head;
};

struct iwl_fmac_ftm_responder_cfg {
	u32 cmd_valid_fields;
	u32 responder_cfg_flags;
};

struct iwl_fmac_ftm_data {
	u64 cur_cookie;
	u8 cur_req_id;
	u8 num_active_targets;
	struct cfg80211_ftm_target *active_targets;
	u8 enable_dyn_ack;
	u8 algo_type;
};

#ifdef CONFIG_THERMAL
/**
 * struct iwl_fmac_cooling_device
 * @cur_state: current state
 * @cdev: struct thermal cooling device
 */
struct iwl_fmac_cooling_device {
	u32 cur_state;
	struct thermal_cooling_device *cdev;
};

/**
 * struct iwl_fmac_thermal_device - thermal zone related data
 * @temp_trips: temperature thresholds for report
 * @fw_trips_index: keep indexes to original array - temp_trips
 * @tzone: thermal zone device data
 * @notify_thermal_wk: worker to notify thermal manager about
 *	the threshold crossed
 * @ths_crossed: index of threshold crossed
*/
struct iwl_fmac_thermal_device {
	s16 temp_trips[IWL_MAX_DTS_TRIPS];
	u8 fw_trips_index[IWL_MAX_DTS_TRIPS];
	struct thermal_zone_device *tzone;
	struct work_struct notify_thermal_wk;
	u32 ths_crossed;
};

#endif

struct iwl_fmac {
	/* for logger access */
	struct device *dev;

	struct iwl_trans *trans;
	const struct iwl_fw *fw;
	const struct iwl_cfg *cfg;
	struct iwl_phy_db *phy_db;

	/* for protecting access to iwl_fmac */
	struct mutex mutex;

	/* reference counting of netdev queues stop requests */
	atomic_t netdev_q_stop[AC_NUM];

	atomic_t open_count;

	int sta_generation;

	spinlock_t async_handlers_lock; /* protects handlers list */
	struct list_head async_handlers_list;
	struct work_struct async_handlers_wk;

	struct iwl_notif_wait_data notif_wait;

	/* firmware related */
	u32 error_event_table[2];
	u32 umac_error_event_table;

	/* scan */
	struct cfg80211_scan_request *scan_request;

	/* NVM */
	const char *nvm_file_name;
	struct iwl_nvm_data *nvm_data;
	/* EEPROM MAC addresses */
#define IWL_MAX_ADDRESSES		5
	struct mac_address addresses[IWL_MAX_ADDRESSES];

	/* NVM sections */
	struct iwl_nvm_section nvm_sections[NVM_MAX_NUM_SECTIONS];

	struct iwl_fw_runtime fwrt;

	/* Power */
	int user_power_level; /* in dBm, for all interfaces */

	struct work_struct restart_wk;
	unsigned long status;

	bool shutdown;
	bool calibrating;

	struct iwl_fmac_reorder_buffer *reorder_bufs[IWL_MAX_BAID];

	struct iwl_fmac_sta __rcu *stas[IWL_FMAC_MAX_STA];
	u8 queue_sta_map[IWL_MAX_TVQM_QUEUES];

	/* data path */
	unsigned long sta_deferred_frames[BITS_TO_LONGS(IWL_FMAC_MAX_STA)];
	struct work_struct add_stream_wk; /* To add streams to queues */
	/* configured by cfg80211 */
	u32 rts_threshold;

	/* regulatory */
	enum iwl_fmac_mcc_source mcc_src;

	/* NAN */
	struct iwl_fmac_vif *nan_vif;

	/* CT-kill */
	struct delayed_work ct_kill_exit;
#ifdef CONFIG_THERMAL
	struct iwl_fmac_cooling_device cooling_dev;
	struct iwl_fmac_thermal_device tz_device;
#endif

#ifdef CPTCFG_CFG80211_DEBUGFS
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_dir_stations;
	u8 fw_debug_level;
#endif
#if defined(CPTCFG_CFG80211_DEBUGFS) || \
    defined(CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES)
	bool internal_cmd_to_host;
#endif

	/* ts of the beginning of a non-collect fw dbg data period */
	unsigned long fw_dbg_non_collect_ts_start[FW_DBG_TRIGGER_MAX - 1];

	u64 msrment_cookie_counter;
	struct iwl_fmac_ftm_data ftm_data;

	struct iwl_fmac_vif __rcu *monitor_vif;
};

static inline struct iwl_fmac *iwl_fmac_from_opmode(struct iwl_op_mode *opmode)
{
	return (void *)opmode->op_mode_specific;
}

static inline struct iwl_fmac *iwl_fmac_from_wiphy(struct wiphy *wiphy)
{
	return iwl_fmac_from_opmode((void *)wiphy->priv);
}

static inline struct wiphy *wiphy_from_fmac(struct iwl_fmac *fmac)
{
	struct iwl_op_mode *opmode = container_of((void *)fmac,
						  struct iwl_op_mode,
						  op_mode_specific);

	return priv_to_wiphy(opmode);
}

enum iwl_fmac_connect_state {
	IWL_FMAC_CONNECT_IDLE,
	IWL_FMAC_CONNECT_CONNECTING,
	IWL_FMAC_CONNECT_CONNECTED,
};

struct iwl_fmac_vif_mgd {
	struct iwl_fmac_sta __rcu *ap_sta;
	u8 wmm_acm;
	enum iwl_fmac_connect_state connect_state;
};

enum iwl_fmac_ap_state {
	IWL_FMAC_AP_STOPPED,
	IWL_FMAC_AP_STARTING,
	IWL_FMAC_AP_STARTED,
};

#define MCAST_STA_ADDR (const u8 *)"\x03\x00\x00\x00\x00\x00"

struct iwl_fmac_vif_ap {
	struct iwl_fmac_sta mcast_sta;
	enum iwl_fmac_ap_state state;
	bool isolate;
	struct cfg80211_ftm_responder_params responder_params;
	bool start_responder;
	u8 *beacon;
	int head_len, tail_len;
	u8 *head, *tail;
};

struct iwl_fmac_nan_ndp_state {
	struct list_head list;
	u8 ndp_id;
	u8 peer_nmi[ETH_ALEN];
	u8 init_ndi[ETH_ALEN];
	u8 resp_ndi[ETH_ALEN];
	bool active;
};

struct iwl_fmac_vif_nan {
	struct cfg80211_nan_conf conf;
	struct list_head funcs;
	u8 cluster_id[2];
};

enum iwl_fmac_ibss_state {
	IWL_FMAC_IBSS_IDLE,
	IWL_FMAC_IBSS_JOINING,
	IWL_FMAC_IBSS_JOINED,
};

struct iwl_fmac_vif_ibss {
	struct iwl_fmac_sta mcast_sta;
	enum iwl_fmac_ibss_state state;
	u8 bssid[ETH_ALEN];
};

struct iwl_fmac_vif_nan_data {
	/* all accesses need to be protected by the fmac mutex */
	struct list_head ndps_list;
};

struct iwl_fmac_vif {
	u8 addr[ETH_ALEN];
	u8 id;
	struct iwl_fmac_qos_map __rcu *qos_map;
	struct wireless_dev wdev;
	struct iwl_fmac *fmac;
	union {
		struct iwl_fmac_vif_mgd mgd;
		struct iwl_fmac_vif_ap ap;
		struct iwl_fmac_vif_nan nan;
		struct iwl_fmac_vif_ibss ibss;
		struct iwl_fmac_vif_nan_data nan_data;
	};

	int user_power_level; /* in dBm */

	struct iwl_fmac_chandef chandef;
	struct iwl_fmac_ftm_responder_cfg resp_config;
};

#define FMAC_VIF_ID_INVALID U8_MAX
#define FMAC_NAN_DUMMY_VIF_ID (FMAC_VIF_ID_INVALID - 1)

static inline struct iwl_fmac_vif *vif_from_netdev(struct net_device *dev)
{
	return netdev_priv(dev);
}

static inline struct iwl_fmac_vif *vif_from_wdev(struct wireless_dev *wdev)
{
	return container_of(wdev, struct iwl_fmac_vif, wdev);
}

int iwl_fmac_nl_to_fmac_type(enum nl80211_iftype iftype);

static inline struct iwl_fmac_sta *iwl_get_sta(struct iwl_fmac *fmac,
					       const u8 *addr)
{
	struct iwl_fmac_sta *sta;
	int tmp;

	for_each_valid_sta(fmac, sta, tmp) {
		if (ether_addr_equal(sta->addr, addr))
			return sta;
	}

	return NULL;
}

static inline bool iwl_fmac_is_radio_killed(struct iwl_fmac *fmac)
{
	return test_bit(IWL_STATUS_HW_RFKILL, &fmac->status) ||
	       test_bit(IWL_STATUS_HW_CTKILL, &fmac->status);
}

static inline bool iwl_fmac_firmware_running(struct iwl_fmac *fmac)
{
	lockdep_assert_held(&fmac->mutex);
	return iwl_trans_fw_running(fmac->trans);
}

extern struct cfg80211_ops iwl_fmac_cfg_ops;

/**
 * struct iwl_fmac_mod_params - module parameters for iwlfmac
 */
struct iwl_fmac_mod_params {
	/**
	 * @power_scheme: see &enum fmac_ps_mode
	 */
	int power_scheme;
	/**
	 * @init_dbg: true to keep the device awake after
	 *	an ASSERT in INIT image
	 */
	bool init_dbg;
	/**
	 * @ftm_resp_asap: value for IWL_FMAC_CONFIG_VIF_FTM_RESP_ASAP
	 */
	bool ftm_resp_asap;
	/**
	 * @host_based_ap: true if the AP mode should be host_based
	 */
	bool host_based_ap;
};

extern struct iwl_fmac_mod_params iwlfmac_mod_params;

void iwl_fmac_setup_wiphy(struct iwl_fmac *fmac);

netdev_tx_t iwl_fmac_dev_start_xmit(struct sk_buff *skb,
				    struct net_device *dev);
void iwl_fmac_tx_send_frame(struct iwl_fmac *fmac,
			    struct iwl_fmac_send_frame_notif *send_frame);

void iwl_fmac_rx_tx_cmd(struct iwl_fmac *fmac, struct iwl_rx_cmd_buffer *rxb);
void iwl_fmac_rx_ba_notif(struct iwl_fmac *fmac, struct iwl_rx_cmd_buffer *rxb);
void iwl_fmac_mfu_assert_dump_notif(struct iwl_fmac *fmac,
				    struct iwl_rx_cmd_buffer *rxb);
void iwl_fmac_rx_mpdu(struct iwl_fmac *fmac, struct napi_struct *napi,
		      struct iwl_rx_cmd_buffer *rxb, int queue);
void iwl_fmac_rx_frame_release(struct iwl_fmac *fmac, struct napi_struct *napi,
			       struct iwl_rx_packet *pkt, int queue);
void iwl_fmac_rx_delba_ntfy(struct iwl_fmac *fmac, struct iwl_rx_packet *pkt,
			    int queue);
void iwl_fmac_destroy_reorder_buffer(struct iwl_fmac *fmac,
				     struct iwl_fmac_sta *sta,
				     struct iwl_fmac_reorder_buffer *buf);

void iwl_fmac_add_new_stream_wk(struct work_struct *wk);
/**
 * struct iwl_fmac_skb_info - driver data per skb
 * @dev_cmd: a pointer to the iwl_dev_cmd associated with this skb
 * @sta: pointer to the station
 * @key: pointer to the key
 * @trans: transport data
 */
struct iwl_fmac_skb_info {
	struct iwl_device_cmd *dev_cmd;
	struct iwl_fmac_sta *sta;
	struct iwl_fmac_sta_key *key;
	void *trans[2];
};


int iwl_fmac_tx_skb(struct iwl_fmac *fmac, struct sk_buff *skb);

struct net_device *iwl_fmac_create_netdev(struct iwl_fmac *fmac,
					  const char *name,
					  unsigned char name_assign_type,
					  enum nl80211_iftype iftype,
					  struct vif_params *params);
struct wireless_dev *
iwl_fmac_create_non_netdev_iface(struct iwl_fmac *fmac,
				 struct vif_params *params,
				 enum nl80211_iftype iftype);
void iwl_fmac_destroy_vif(struct iwl_fmac_vif *vif);
void iwl_fmac_nic_restart(struct iwl_fmac *fmac);

/* firmware functions */
int iwl_fmac_run_init_fw(struct iwl_fmac *fmac);
int iwl_fmac_run_rt_fw(struct iwl_fmac *fmac);
void iwl_fmac_stop_device(struct iwl_fmac *fmac);
int iwl_fmac_send_cmd(struct iwl_fmac *fmac, struct iwl_host_cmd *cmd);
int iwl_fmac_send_cmd_pdu(struct iwl_fmac *fmac, u32 id,
			  u32 flags, u16 len, const void *data);
int iwl_fmac_send_cmd_status(struct iwl_fmac *fmac, struct iwl_host_cmd *cmd,
			     u32 *status);
int iwl_fmac_send_cmd_pdu_status(struct iwl_fmac *fmac, u32 id, u16 len,
				 const void *data, u32 *status);
void iwl_fmac_dump_nic_error_log(struct iwl_fmac *fmac);
struct ieee80211_regdomain *
iwl_fmac_set_regdom(struct iwl_fmac *fmac, const char *mcc,
		    enum iwl_fmac_mcc_source src_id);

int iwl_fmac_send_config_cmd(struct iwl_fmac *fmac,
			     u8 vif_id, enum iwl_fmac_config_id config_id,
			     const void *data, u16 len);

static inline int
iwl_fmac_send_config_u32(struct iwl_fmac *fmac,
			 u8 vif_id, enum iwl_fmac_config_id config_id,
			 u32 value)
{
	__le32 _value = cpu_to_le32(value);

	return iwl_fmac_send_config_cmd(fmac, vif_id, config_id,
					&_value, sizeof(u32));
}

static inline bool iwl_fmac_has_new_tx_api(struct iwl_fmac *fmac)
{
	/* TODO - replace with TLV once defined */
	return fmac->trans->cfg->use_tfh;
}

static inline bool iwl_fmac_has_unified_ucode(struct iwl_fmac *fmac)
{
	return fmac->trans->cfg->device_family >= IWL_DEVICE_FAMILY_22000;
}

/* vendor cmd */
void iwl_fmac_set_wiphy_vendor_commands(struct wiphy *wiphy);

u32 iwl_fmac_get_phy_config(struct iwl_fmac *fmac);
u8 iwl_fmac_get_valid_tx_ant(struct iwl_fmac *fmac);

/* NVM */
int iwl_fmac_nvm_init(struct iwl_fmac *fmac, bool read_nvm_from_nic);
int iwl_fmac_load_nvm_to_nic(struct iwl_fmac *fmac);
int iwl_fmac_send_nvm_cmd(struct iwl_fmac *fmac);

/* NAN */
int iwl_fmac_nan_config_cmd(struct iwl_fmac *fmac, struct iwl_fmac_vif *vif,
			    struct cfg80211_nan_conf *conf);

int iwl_fmac_start_nan(struct wiphy *wiphy, struct wireless_dev *wdev,
		       struct cfg80211_nan_conf *conf);
void iwl_fmac_stop_nan(struct wiphy *wiphy,
		       struct wireless_dev *wdev);
int iwl_fmac_add_nan_func(struct wiphy *wiphy,
			  struct wireless_dev *wdev,
			  struct cfg80211_nan_func *nan_func);
void iwl_fmac_del_nan_func(struct wiphy *wiphy,
			   struct wireless_dev *wdev,
			   u64 cookie);
void iwl_fmac_nan_de_term_notif(struct iwl_fmac *fmac,
				struct iwl_rx_cmd_buffer *rxb);
void iwl_fmac_nan_match(struct iwl_fmac *fmac, struct iwl_rx_cmd_buffer *rxb);
void iwl_fmac_term_all_nan_func(struct iwl_fmac *fmac,
				struct wireless_dev *wdev,
				enum nl80211_nan_func_term_reason reason);

/* STA */
int iwl_fmac_alloc_sta(struct iwl_fmac *fmac, struct iwl_fmac_vif *vif,
		       u8 sta_id, u8 *addr);
void iwl_fmac_free_sta(struct iwl_fmac *fmac, u8 sta_id, bool hw_error);
void iwl_fmac_free_stas(struct iwl_fmac *fmac,
			struct iwl_fmac_vif *vif,
			bool hw_error);
int iwl_fmac_flush_sta_queues(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta);
void iwl_fmac_sta_add_key(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta,
			  bool pairwise, const struct iwl_fmac_key *fw_key);
void iwl_fmac_sta_rm_key(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta,
			 bool pairwise, u8 keyidx);
void iwl_fmac_destroy_sta_keys(struct iwl_fmac *fmac,
			       struct iwl_fmac_sta *sta);

/* AP */
void iwl_fmac_clear_ap_state(struct iwl_fmac *fmac, struct iwl_fmac_vif *vif);

/* IBSS */
void iwl_fmac_clear_ibss_state(struct iwl_fmac *fmac, struct iwl_fmac_vif *vif);

/* TXQ */
struct iwl_fmac_txq_scd_cfg {
	u8 vif_id;
	u8 fifo;
	u8 sta_id;
	u8 tid;
	bool aggregate;
	int frame_limit;
};

void iwl_fmac_release_txq(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta,
			  int queue, u8 tid);
int iwl_fmac_alloc_queue(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta,
			 u8 tid, struct ieee80211_hdr *hdr);
void iwl_fmac_stop_ac_queue(struct iwl_fmac *fmac, struct wireless_dev *wdev,
			    int ac);
void iwl_fmac_wake_ac_queue(struct iwl_fmac *fmac, struct wireless_dev *wdev,
			    int ac);

/* scan */
int iwl_fmac_abort_scan(struct iwl_fmac *fmac, struct iwl_fmac_vif *vif);

/* thermal */
void iwl_fmac_thermal_initialize(struct iwl_fmac *fmac);
void iwl_fmac_thermal_exit(struct iwl_fmac *fmac);
void iwl_fmac_ct_kill_notif(struct iwl_fmac *fmac,
			    struct iwl_rx_cmd_buffer *rxb);
void iwl_fmac_enter_ctkill(struct iwl_fmac *fmac);
int iwl_fmac_ctdp_command(struct iwl_fmac *fmac, u32 op, u32 state);
int iwl_fmac_get_temp(struct iwl_fmac *fmac, s32 *temp);
#ifdef CONFIG_THERMAL
void iwl_fmac_temp_notif(struct iwl_fmac *fmac,
			 struct iwl_rx_cmd_buffer *rxb);
#endif

/* debugfs */
#ifdef CPTCFG_CFG80211_DEBUGFS
void iwl_fmac_dbgfs_init(struct iwl_fmac *fmac);
void iwl_fmac_dbgfs_exit(struct iwl_fmac *fmac);
void iwl_fmac_dbgfs_add_sta(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta);
void iwl_fmac_dbgfs_add_vif_configuration(struct iwl_fmac *fmac,
					  struct iwl_fmac_vif *vif);

void iwl_fmac_dbgfs_del_sta(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta);
#else
static inline void iwl_fmac_dbgfs_init(struct iwl_fmac *fmac) {}
static inline void iwl_fmac_dbgfs_exit(struct iwl_fmac *fmac) {}
static inline void iwl_fmac_dbgfs_add_sta(struct iwl_fmac *fmac,
					  struct iwl_fmac_sta *sta) {}
static inline void
iwl_fmac_dbgfs_add_vif_configuration(struct iwl_fmac *fmac,
				     struct iwl_fmac_vif *vif) {}

static inline void iwl_fmac_dbgfs_del_sta(struct iwl_fmac *fmac,
					  struct iwl_fmac_sta *sta) {}
#endif

void iwl_fmac_process_async_handlers(struct iwl_fmac *fmac);

int
iwl_fmac_send_ftm_responder_start(struct iwl_fmac *fmac,
				  struct iwl_fmac_vif *vif,
				  struct cfg80211_ftm_responder_params *params);

void
iwl_fmac_neighbor_report_response(struct iwl_fmac *fmac,
				  struct iwl_fmac_neighbor_rep_notify *notif);

void iwl_fmac_reset_ftm_data(struct iwl_fmac *fmac);
int iwl_fmac_perform_ftm(struct iwl_fmac *fmac, struct iwl_fmac_vif *vif,
			 u64 cookie, struct cfg80211_ftm_request *req);
int iwl_fmac_abort_ftm(struct iwl_fmac *fmac, struct iwl_fmac_vif *vif,
		       u64 cookie);
void iwl_fmac_rx_ftm_response(struct iwl_fmac *fmac,
			      struct iwl_rx_cmd_buffer *rxb);
u8 cfg_width_to_iwl_width(enum nl80211_chan_width cfg_width);

void iwl_fmac_disconnected(struct iwl_fmac *fmac, struct iwl_fmac_sta *sta,
			   __le16 reason, u8 locally_generated);

int iwl_fmac_nan_ndp(struct wiphy *wiphy, struct wireless_dev *wdev,
		     struct cfg80211_nan_ndp_params *params);
void iwl_fmac_nan_data_stop(struct wiphy *wiphy, struct net_device *dev);
void iwl_fmac_nan_ndp_notify(struct iwl_fmac *fmac,
			     struct iwl_rx_cmd_buffer *rxb);
void iwl_fmac_nan_cluster_notify(struct iwl_fmac *fmac,
				 struct iwl_rx_cmd_buffer *rxb);
#endif /* __IWL_FMAC_H__ */
