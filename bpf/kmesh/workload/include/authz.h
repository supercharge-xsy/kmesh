/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright Authors of Kmesh */

#ifndef __AUTHZ_H__
#define __AUTHZ_H__

#include "workload_common.h"
#include "bpf_log.h"
#include "workloadapi/security/authorization.pb-c.h"

#define AUTH_ALLOW   0
#define AUTH_DENY    1
#define UNMATCHED    0
#define MATCHED      1
#define UNSUPPORTED  2
#define TYPE_SRCIP   (1)
#define TYPE_DSTIP   (1 << 1)
#define TYPE_DSTPORT (1 << 2)

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, AUTHZ_NAME_MAX_LEN);
    __uint(value_size, sizeof(Istio__Security__Authorization));
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __uint(max_entries, MAP_SIZE_OF_AUTH);
} map_of_authz SEC(".maps");

static inline Istio__Security__Authorization *map_lookup_authz(const char *key)
{
    return (Istio__Security__Authorization *)kmesh_map_lookup_elem(&map_of_authz, key);
}

static inline wk_policies_v *get_workload_policies_by_uid(__u32 workload_uid)
{
    return (wk_policies_v *)kmesh_map_lookup_elem(&map_of_workload_policy, &workload_uid);
}

static inline int matchDstPorts(Istio__Security__Match *match, struct bpf_sock_tuple *tuple_info)
{
    __u32 *notPorts = NULL;
    __u32 *ports = NULL;
    __u32 i;

    if (match->n_destination_ports == 0 && match->n_not_destination_ports == 0) {
        return MATCHED;
    }

    if (match->n_not_destination_ports != 0) {
        notPorts = kmesh_get_ptr_val(match->not_destination_ports);
        if (!notPorts) {
            BPF_LOG(ERR, AUTH, "failed to get not_destination_ports ptr\n");
            return UNMATCHED;
        }
#pragma unroll
        for (i = 0; i < MAX_MEMBER_NUM_PER_POLICY; i++) {
            if (i >= match->n_not_destination_ports) {
                break;
            }

            if (bpf_htons(notPorts[i]) == tuple_info->ipv4.dport || bpf_htons(notPorts[i]) == tuple_info->ipv6.dport) {
                BPF_LOG(INFO, AUTH, "match not_ports: %d\n", notPorts[i]);
                return UNMATCHED;
            }
        }
    }

    if (match->n_destination_ports != 0) {
        ports = kmesh_get_ptr_val(match->destination_ports);
        if (!ports) {
            BPF_LOG(ERR, AUTH, "failed to get destination_ports ptr\n");
            return UNMATCHED;
        }
#pragma unroll
        for (i = 0; i < MAX_MEMBER_NUM_PER_POLICY; i++) {
            if (i >= match->n_destination_ports) {
                break;
            }
            if (bpf_htons(ports[i]) == tuple_info->ipv4.dport || bpf_htons(ports[i]) == tuple_info->ipv6.dport) {
                BPF_LOG(INFO, AUTH, "match ports: %d\n", ports[i]);
                return MATCHED;
            }
        }
    }
    return UNMATCHED;
}

static inline int match_check(Istio__Security__Match *match, struct bpf_sock_tuple *tuple_info)
{
    __u32 matchResult;

    // if multiple types are set, they are AND-ed, all matched is a match
    // todo: add other match types
    matchResult = matchDstPorts(match, tuple_info);
    return matchResult;
}

static inline int clause_match_check(Istio__Security__Clause *cl, struct bpf_sock_tuple *tuple_info)
{
    void *matchsPtr = NULL;
    Istio__Security__Match *match = NULL;
    __u32 i;

    if (cl->n_matches == 0) {
        BPF_LOG(ERR, AUTH, "auth clause has no match info\n");
        return UNMATCHED;
    }
    matchsPtr = kmesh_get_ptr_val(cl->matches);
    if (!matchsPtr) {
        BPF_LOG(ERR, AUTH, "failed to get matches from clause\n");
        return MATCHED;
    }

#pragma unroll
    for (i = 0; i < MAX_MEMBER_NUM_PER_POLICY; i++) {
        if (i >= cl->n_matches) {
            break;
        }
        match = (Istio__Security__Match *)kmesh_get_ptr_val((void *)*((__u64 *)matchsPtr + i));
        if (!match) {
            continue;
        }
        // if any match matches, it is a match
        if (match_check(match, tuple_info) == MATCHED) {
            return MATCHED;
        }
    }
    return UNMATCHED;
}

static inline int rule_match_check(Istio__Security__Rule *rule, struct bpf_sock_tuple *tuple_info)
{
    void *clausesPtr = NULL;
    Istio__Security__Clause *clause = NULL;
    __u32 i;

    if (rule->n_clauses == 0) {
        BPF_LOG(ERR, AUTH, "rule has no clauses\n");
        return UNMATCHED;
    }
    // Clauses are AND-ed.
    clausesPtr = kmesh_get_ptr_val(rule->clauses);
    if (!clausesPtr) {
        BPF_LOG(ERR, AUTH, "failed to get clauses from rule\n");
        return UNMATCHED;
    }

#pragma unroll
    for (i = 0; i < MAX_MEMBER_NUM_PER_POLICY; i++) {
        if (i >= rule->n_clauses) {
            break;
        }
        clause = (Istio__Security__Clause *)kmesh_get_ptr_val((void *)*((__u64 *)clausesPtr + i));
        if (!clause) {
            continue;
        }

        if (clause_match_check(clause, tuple_info) == UNMATCHED) {
            return UNMATCHED;
        }
    }
    return MATCHED;
}

static inline int policy_manage(Istio__Security__Authorization *policy, struct bpf_sock_tuple *tuple_info)
{
    void *rulesPtr = NULL;
    Istio__Security__Rule *rule = NULL;
    int matchFlag = 0;
    __u32 i = 0;

    if (policy->n_rules == 0) {
        BPF_LOG(ERR, AUTH, "auth policy %s has no rules\n", kmesh_get_ptr_val(policy->name));
        return AUTH_ALLOW;
    }

    // Rules are OR-ed.
    rulesPtr = kmesh_get_ptr_val(policy->rules);
    if (!rulesPtr) {
        BPF_LOG(ERR, AUTH, "failed to get rules from policy %s\n", kmesh_get_ptr_val(policy->name));
        return AUTH_DENY;
    }

#pragma unroll
    for (i = 0; i < 1; i++) {
        if (i >= policy->n_rules) {
            break;
        }
        rule = (Istio__Security__Rule *)kmesh_get_ptr_val((void *)*((__u64 *)rulesPtr + i));
        if (!rule) {
            continue;
        }
        if (rule_match_check(rule, tuple_info) == MATCHED) {
            if (policy->action == ISTIO__SECURITY__ACTION__DENY) {
                return AUTH_DENY;
            } else {
                return AUTH_ALLOW;
            }
        }
    }

    // no match rules
    if (policy->action == ISTIO__SECURITY__ACTION__DENY) {
        return AUTH_ALLOW;
    } else {
        return AUTH_DENY;
    }
}

#endif