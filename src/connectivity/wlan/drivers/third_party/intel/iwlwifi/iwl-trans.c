/******************************************************************************
 *
 * Copyright(c) 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-constants.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-fh.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"

#if 0   // NEEDS_PORTING
struct iwl_trans* iwl_trans_alloc(unsigned int priv_size, struct device* dev,
                                  const struct iwl_cfg* cfg, const struct iwl_trans_ops* ops) {
    struct iwl_trans* trans;
#ifdef CONFIG_LOCKDEP
    static struct lock_class_key __key;
#endif

    trans = devm_kzalloc(dev, sizeof(*trans) + priv_size, GFP_KERNEL);
    if (!trans) { return NULL; }

#ifdef CONFIG_LOCKDEP
    lockdep_init_map(&trans->sync_cmd_lockdep_map, "sync_cmd_lockdep_map", &__key, 0);
#endif

    trans->dev = dev;
    trans->cfg = cfg;
    trans->ops = ops;
    trans->num_rx_queues = 1;

    snprintf(trans->dev_cmd_pool_name, sizeof(trans->dev_cmd_pool_name), "iwl_cmd_pool:%s",
             dev_name(trans->dev));
    trans->dev_cmd_pool = kmem_cache_create(trans->dev_cmd_pool_name, sizeof(struct iwl_device_cmd),
                                            sizeof(void*), SLAB_HWCACHE_ALIGN, NULL);
    if (!trans->dev_cmd_pool) { return NULL; }

    WARN_ON(!ops->wait_txq_empty && !ops->wait_tx_queues_empty);

    return trans;
}

void iwl_trans_free(struct iwl_trans* trans) {
    kmem_cache_destroy(trans->dev_cmd_pool);
}

int iwl_trans_send_cmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
    int ret;

    if (unlikely(!(cmd->flags & CMD_SEND_IN_RFKILL) &&
                 test_bit(STATUS_RFKILL_OPMODE, &trans->status))) {
        return -ERFKILL;
    }

    if (unlikely(test_bit(STATUS_FW_ERROR, &trans->status))) { return -EIO; }

    if (unlikely(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return -EIO;
    }

    if (WARN_ON((cmd->flags & CMD_WANT_ASYNC_CALLBACK) && !(cmd->flags & CMD_ASYNC))) {
        return -EINVAL;
    }

    if (!(cmd->flags & CMD_ASYNC)) { lock_map_acquire_read(&trans->sync_cmd_lockdep_map); }

    if (trans->wide_cmd_header && !iwl_cmd_groupid(cmd->id)) { cmd->id = DEF_ID(cmd->id); }

    ret = trans->ops->send_cmd(trans, cmd);

    if (!(cmd->flags & CMD_ASYNC)) { lock_map_release(&trans->sync_cmd_lockdep_map); }

    if (WARN_ON((cmd->flags & CMD_WANT_SKB) && !ret && !cmd->resp_pkt)) { return -EIO; }

    return ret;
}
IWL_EXPORT_SYMBOL(iwl_trans_send_cmd);

/* Comparator for struct iwl_hcmd_names.
 * Used in the binary search over a list of host commands.
 *
 * @key: command_id that we're looking for.
 * @elt: struct iwl_hcmd_names candidate for match.
 *
 * @return 0 iff equal.
 */
static int iwl_hcmd_names_cmp(const void* key, const void* elt) {
    const struct iwl_hcmd_names* name = elt;
    uint8_t cmd1 = *(uint8_t*)key;
    uint8_t cmd2 = name->cmd_id;

    return (cmd1 - cmd2);
}

const char* iwl_get_cmd_string(struct iwl_trans* trans, uint32_t id) {
    uint8_t grp, cmd;
    struct iwl_hcmd_names* ret;
    const struct iwl_hcmd_arr* arr;
    size_t size = sizeof(struct iwl_hcmd_names);

    grp = iwl_cmd_groupid(id);
    cmd = iwl_cmd_opcode(id);

    if (!trans->command_groups || grp >= trans->command_groups_size ||
        !trans->command_groups[grp].arr) {
        return "UNKNOWN";
    }

    arr = &trans->command_groups[grp];
    ret = bsearch(&cmd, arr->arr, arr->size, size, iwl_hcmd_names_cmp);
    if (!ret) { return "UNKNOWN"; }
    return ret->cmd_name;
}
IWL_EXPORT_SYMBOL(iwl_get_cmd_string);

int iwl_cmd_groups_verify_sorted(const struct iwl_trans_config* trans) {
    int i, j;
    const struct iwl_hcmd_arr* arr;

    for (i = 0; i < trans->command_groups_size; i++) {
        arr = &trans->command_groups[i];
        if (!arr->arr) { continue; }
        for (j = 0; j < arr->size - 1; j++)
            if (arr->arr[j].cmd_id > arr->arr[j + 1].cmd_id) { return -1; }
    }
    return 0;
}
IWL_EXPORT_SYMBOL(iwl_cmd_groups_verify_sorted);
#endif  // NEEDS_PORTING

void iwl_trans_ref(struct iwl_trans* trans) {
    if (trans->ops->ref) { trans->ops->ref(trans); }
}

void iwl_trans_unref(struct iwl_trans* trans) {
    if (trans->ops->unref) { trans->ops->unref(trans); }
}
