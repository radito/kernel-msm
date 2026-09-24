// SPDX-License-Identifier: GPL-2.0
/* SELinux-hiding compatibility for KernelSU-Next 3.2 legacy on Redbull 4.19.
 * Keep the upstream feature ID and the original policy independent of the
 * live policy so KernelSU's rules cannot change app-facing SELinux queries.
 */
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>

#include "security.h"
#include "ss/policydb.h"
#include "ss/services.h"
#include "ss/sidtab.h"
#include "policy/feature.h"

/* KSU_FEATURE_SELINUX_HIDE in KernelSU-Next v3.3.0. */
#define KSU_FEATURE_SELINUX_HIDE 4
#define KSU_APP_UID_MIN 10000

static struct selinux_state hide_state;
static bool hide_ready;
static bool hide_enabled;

static bool ksu_hide_app(void)
{
	return READ_ONCE(hide_enabled) && smp_load_acquire(&hide_ready) &&
		__kuid_val(current_uid()) >= KSU_APP_UID_MIN;
}

void ksu_hide_snapshot(struct selinux_state *state, const void *data, size_t len)
{
	struct selinux_ss *ss;
	struct selinux_kernel_status *status;
	struct policy_file fp = { (void *)data, len };
	int err;

	if (smp_load_acquire(&hide_ready) || !state->initialized)
		return;

	ss = kzalloc(sizeof(*ss), GFP_KERNEL);
	if (!ss)
		return;
	rwlock_init(&ss->policy_rwlock);
	mutex_init(&ss->status_lock);

	err = policydb_read(&ss->policydb, &fp);
	if (err)
		goto free_ss;
	ss->policydb.len = len;

	ss->sidtab = kmalloc(sizeof(*ss->sidtab), GFP_KERNEL);
	if (!ss->sidtab)
		goto destroy_policy;
	err = policydb_load_isids(&ss->policydb, ss->sidtab);
	if (err)
		goto free_sidtab;

	ss->map.size = state->ss->map.size;
	ss->map.mapping = kmemdup(state->ss->map.mapping,
				  sizeof(*ss->map.mapping) * ss->map.size,
				  GFP_KERNEL);
	if (!ss->map.mapping)
		goto destroy_sidtab;

	ss->status_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!ss->status_page)
		goto free_mapping;
	status = page_address(ss->status_page);
	status->version = SELINUX_KERNEL_STATUS_VERSION;
	status->enforcing = enforcing_enabled(state);
	status->policyload = state->ss->latest_granting;
	status->deny_unknown = !ss->policydb.allow_unknown;
	ss->latest_granting = state->ss->latest_granting;

	enforcing_set(&hide_state, enforcing_enabled(state));
	hide_state.initialized = true;
	hide_state.ss = ss;
	smp_store_release(&hide_ready, true);
	pr_info("KernelSU: clean SELinux policy snapshot ready\n");
	return;

free_mapping:
	kfree(ss->map.mapping);
destroy_sidtab:
	sidtab_destroy(ss->sidtab);
free_sidtab:
	kfree(ss->sidtab);
destroy_policy:
	policydb_destroy(&ss->policydb);
free_ss:
	kfree(ss);
	pr_warn("KernelSU: clean SELinux policy snapshot unavailable\n");
}

struct selinux_state *ksu_hide_app_state(struct selinux_state *state)
{
	return ksu_hide_app() ? &hide_state : state;
}

struct page *ksu_hide_app_status_page(struct page *status)
{
	return ksu_hide_app() ? hide_state.ss->status_page : status;
}

int ksu_hide_check_setprocattr(const char *name, const void *value, size_t size)
{
	u32 sid;

	if (!ksu_hide_app() || strcmp(name, "current"))
		return 0;
	return security_context_to_sid(&hide_state, value, size, &sid, GFP_KERNEL);
}

static int ksu_hide_get(u64 *value)
{
	*value = READ_ONCE(hide_enabled);
	return 0;
}

static int ksu_hide_set(u64 value)
{
	if (value > 1)
		return -EINVAL;
	if (value && !smp_load_acquire(&hide_ready))
		return -EAGAIN;
	WRITE_ONCE(hide_enabled, !!value);
	return 0;
}

static const struct ksu_feature_handler ksu_hide_feature = {
	.feature_id = KSU_FEATURE_SELINUX_HIDE,
	.name = "selinux_hide",
	.get_handler = ksu_hide_get,
	.set_handler = ksu_hide_set,
};

static int __init ksu_hide_init(void)
{
	return ksu_register_feature_handler(&ksu_hide_feature);
}
late_initcall(ksu_hide_init);
