#include "asn1c_internal.h"
#include "asn1c_naming.h"
#include "asn1c_misc.h"
#include "asn1c_misc.h"
#include <asn1_buffer.h>

struct intl_name {
    asn1p_expr_t *expr;
    asn1p_expr_t *clashes_with;
    const char *name;
    TQ_ENTRY(struct intl_name) next;
};
static TQ_HEAD(struct intl_name) used_names;

void
c_name_clash_finder_init() {
    TQ_INIT(&used_names);
}

static void
register_global_name(arg_t *arg, const char *name) {
    struct intl_name *n;

    TQ_FOR(n, &used_names, next) {
        if(strcmp(n->name, name) == 0) {
            if(!(arg->expr->_mark & TM_NAMEGIVEN) && arg->expr != n->expr) {
                n->clashes_with = arg->expr;
                return;
            }
        }
    }

    n = calloc(1, sizeof(*n));
    assert(n);
    n->expr = arg->expr;
    n->name = strdup(name);
    TQ_ADD(&used_names, n, next);
}

int
c_name_clash(arg_t *arg) {
    struct intl_name *n;
    size_t n_clashes = 0;
    const size_t max_clashes = 5;

    TQ_FOR(n, &used_names, next) {
        if(n->clashes_with) {
            if(n_clashes++ > max_clashes) continue;
            FATAL(
                "Name \"%s\" is generated by %s.%s at line %s:%d and "
                "%s.%s at line %s:%d",
                n->name, n->expr->module->ModuleName, n->expr->Identifier,
                n->expr->module->source_file_name, n->expr->_lineno,
                n->clashes_with->module->ModuleName,
                n->clashes_with->Identifier,
                n->clashes_with->module->source_file_name,
                n->clashes_with->_lineno);
        }
    }

    if(n_clashes > max_clashes) {
        FATAL("... %zu more name clashes not shown", n_clashes - max_clashes);
    }

    return n_clashes > 0;
}


static abuf *
construct_base_name(abuf *buf, arg_t *arg, int compound_names,
                    int avoid_keywords) {
    const char *id;

    if(!buf) buf = abuf_new();

    if(compound_names && arg->expr->parent_expr) {
        arg_t tmparg = *arg;
        tmparg.expr = arg->expr->parent_expr;
        construct_base_name(buf, &tmparg, compound_names, 0);
        if(buf->length) {
            abuf_str(buf, "__"); /* component separator */
        }
    }

    id = asn1c_make_identifier(
        ((avoid_keywords && !buf->length) ? AMI_CHECK_RESERVED : 0), arg->expr,
        0);

    abuf_str(buf, id);

    return buf;
}

static struct c_names
c_name_impl(arg_t *arg, int avoid_keywords) {
    asn1p_expr_type_e expr_type = arg->expr->expr_type;
    struct c_names names;
    int compound_names = 0;

    static abuf b_base_name;
    static abuf b_short_name;
    static abuf b_full_name;
    static abuf b_as_member;
    static abuf b_presence_enum;
    static abuf b_presence_name;
    static abuf b_members_enum;
    static abuf b_members_name;

    abuf_clear(&b_base_name);
    abuf_clear(&b_short_name);
    abuf_clear(&b_full_name);
    abuf_clear(&b_as_member);
    abuf_clear(&b_presence_enum);
    abuf_clear(&b_presence_name);
    abuf_clear(&b_members_enum);
    abuf_clear(&b_members_name);

    if((arg->flags & A1C_COMPOUND_NAMES)) {
        if((expr_type & ASN_CONSTR_MASK)
           || expr_type == ASN_BASIC_ENUMERATED
           || ((expr_type == ASN_BASIC_INTEGER
                || expr_type == ASN_BASIC_BIT_STRING))) {
            compound_names = 1;
        }
    }

    abuf *base_name =
        construct_base_name(NULL, arg, compound_names, avoid_keywords);
    abuf *part_name =
        construct_base_name(NULL, arg, compound_names, 0);
    abuf *member_name =
        construct_base_name(NULL, arg, 0, 1);

    abuf_printf(&b_base_name, "%s", base_name->buffer);
    if(!arg->expr->_anonymous_type) {
        if(arg->embed) {
            abuf_printf(&b_short_name, "%s", member_name->buffer);
        } else {
            abuf_printf(&b_short_name, "%s_t", member_name->buffer);
        }
    }
    abuf_printf(&b_full_name, "struct %s", base_name->buffer);
    abuf_printf(&b_as_member, "%s", member_name->buffer);
    abuf_printf(&b_presence_enum, "enum %s_PR", part_name->buffer);
    abuf_printf(&b_presence_name, "%s_PR", part_name->buffer);
    abuf_printf(&b_members_enum, "enum %s", base_name->buffer);
    abuf_printf(&b_members_name, "e_%s", part_name->buffer);

    names.base_name = b_base_name.buffer;
    names.short_name = b_short_name.buffer;
    names.full_name = b_full_name.buffer;
    names.as_member = b_as_member.buffer;
    names.presence_enum = b_presence_enum.buffer;
    names.presence_name = b_presence_name.buffer;
    names.members_enum = b_members_enum.buffer;
    names.members_name = b_members_name.buffer;

    abuf_free(base_name);
    abuf_free(part_name);
    abuf_free(member_name);

    /* A _subset_ of names is checked against being globally unique */
    register_global_name(arg, names.base_name);
    register_global_name(arg, names.full_name);
    register_global_name(arg, names.presence_enum);
    register_global_name(arg, names.presence_name);
    register_global_name(arg, names.members_enum);
    register_global_name(arg, names.members_name);

    arg->expr->_mark |= TM_NAMEGIVEN;

    return names;
}

struct c_names
c_name(arg_t *arg) {
    return c_name_impl(arg, 1);
}

const char *
c_member_name(arg_t *arg, asn1p_expr_t *expr) {
    static abuf ab;

    abuf_clear(&ab);

    abuf_str(&ab, c_name_impl(arg, 0).base_name);
    abuf_str(&ab, "_");
    abuf_str(&ab, asn1c_make_identifier(0, expr, 0));

    return ab.buffer;
}


const char *
c_presence_name(arg_t *arg, asn1p_expr_t *expr) {
    static abuf ab;

    abuf_clear(&ab);

    if(expr) {
        abuf_str(&ab, c_name_impl(arg, 0).base_name);
        abuf_str(&ab, "_PR_");
        abuf_str(&ab, asn1c_make_identifier(0, expr, 0));
    } else {
        abuf_printf(&ab, "%s_PR_NOTHING", c_name_impl(arg, 0).base_name);
    }

    return ab.buffer;
}
