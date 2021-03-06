#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include <mncommon/array.h>
#include <mncommon/hash.h>
#include <mncommon/bytes.h>
#include <mncommon/bytestream.h>

#include <mncommon/dumpm.h>
#include <mncommon/util.h>

#include "diag.h"

#include "mnpbc.h"


static void analyze_backend1(mnpbc_container_t *);


static char *
mnpbc_container_keyword(mnpbc_container_t *cont)
{
    char *w;

    if (cont->kind == MNPBC_CONT_KMESSAGE) {
        w = "struct ";
    } else if (cont->kind == MNPBC_CONT_KENUM) {
        w = "enum ";
    } else if (cont->kind == MNPBC_CONT_KONEOF) {
        w = "union ";
    } else if (cont->kind == MNPBC_CONT_KBUILTIN) {
        w = NULL;
    } else {
        FAIL("print_forward_decl");
    }

    return w;
}


static int
mnpbc_wtype_from_type(mnpbc_container_t *ty)
{
    if (ty->kind == MNPBC_CONT_KMESSAGE ||
        bytes_cmp(ty->pb.name, &_string) == 0 ||
        bytes_cmp(ty->pb.name, &_bytes) == 0) {
        return MNPB_WT_LDELIM;

    } else if (ty->kind == MNPBC_CONT_KENUM ||
               bytes_cmp(ty->pb.name, &_int32) == 0 ||
               bytes_cmp(ty->pb.name, &_int64) == 0 ||
               bytes_cmp(ty->pb.name, &_uint32) == 0 ||
               bytes_cmp(ty->pb.name, &_uint64) == 0 ||
               bytes_cmp(ty->pb.name, &_sint32) == 0 ||
               bytes_cmp(ty->pb.name, &_sint64) == 0 ||
               bytes_cmp(ty->pb.name, &_bool) == 0) {
        return MNPB_WT_VARINT;
    } else if (ty->kind == MNPBC_CONT_KONEOF) {
        return MNPB_WT_INTERN;
    } else if (bytes_cmp(ty->pb.name, &_fixed32) == 0 ||
               bytes_cmp(ty->pb.name, &_sfixed32) == 0 ||
               bytes_cmp(ty->pb.name, &_float) == 0) {
        return MNPB_WT_32BIT;
    } else if (bytes_cmp(ty->pb.name, &_fixed64) == 0 ||
               bytes_cmp(ty->pb.name, &_sfixed64) == 0 ||
               bytes_cmp(ty->pb.name, &_double) == 0) {
        return MNPB_WT_64BIT;
    }
    return MNPB_WT_UNDEF;
}


static mnbytes_t *
mnpbc_module_name_upper(mnpbc_ctx_t *ctx)
{
    mnbytes_t *res;
    res = bytes_new_from_bytes(ctx->namein);
    bytes_tr(
        res,
        (unsigned char *)
        "qwertyuiopasdfghjklzxcvbnm!@#$%^&*()-=_+[]{};\"':,./<>?",
        (unsigned char *)
        "QWERTYUIOPASDFGHJKLZXCVBNM____________________________",
        54);
    return res;
}


UNUSED static mnbytes_t *
mnpbc_module_name_normalized(mnpbc_ctx_t *ctx)
{
    mnbytes_t *res;
    res = bytes_new_from_bytes(ctx->namein);
    bytes_tr(
        res,
        (unsigned char *)
        "!@#$%^&*()-=_+[]{};\"':,./<>?",
        (unsigned char *)
        "____________________________",
        28);
    return res;
}

/*
 * Interface *.proto.h
 */
static mnbytes_t *
get_field_signature(mnpbc_field_t *field)
{
    mnbytes_t *res;
    char *rep;
    char *name;

    if (field->flags.repeated) {
        rep = " *";
        name = "data";
    } else {
        rep = " ";
        name = BCDATA(field->be.name);
    }

    if (field->cty == NULL) {
        /*
         * XXX
         */
        res = bytes_printf("//(external:%s)%s%s", BDATA(field->ty), rep, name);
    } else {
        char *kw;

        kw = mnpbc_container_keyword(field->cty);
        if (kw == NULL) {
            res = bytes_printf("%s%s%s",
                               BDATA(field->cty->be.fqname),
                               rep,
                               name);
        } else {
            if (field->cty->kind == MNPBC_CONT_KONEOF) {
                res = bytes_printf("struct { uint64_t fnum; %s%s%sdata; } %s",
                                   kw,
                                   BDATA(field->cty->be.fqname),
                                   rep,
                                   name);
            } else {
                res = bytes_printf("%s%s%s%s",
                                   kw,
                                   BDATA(field->cty->be.fqname),
                                   rep,
                                   name);
            }
        }
    }

    return res;
}


static void
print_header_pre(mnpbc_ctx_t *ctx, mnbytestream_t *bs)
{
    mnbytes_t *name;
    char *buf =
        "#include <inttypes.h>\n"
        "#include <mncommon/bytes.h>\n"
        "#include <mncommon/bytestream.h>\n"
        "#include <mncommon/util.h>\n\n"
        "#include <mnprotobuf.h>\n\n"
        "#ifdef __GCC__\n"
        "#  pragma GCC diagnostic ignored \"-Wunused-parameter\"\n"
        "#  pragma GCC diagnostic ignored \"-Wunused-variable\"\n"
        "#  pragma GCC diagnostic ignored \"-Wunused-label\"\n"
        "#else\n"
        "#  ifdef __clang__\n"
        "#      pragma clang diagnostic ignored \"-Wunused-parameter\"\n"
        "#      pragma clang diagnostic ignored \"-Wunused-variable\"\n"
        "#      pragma clang diagnostic ignored \"-Wunused-label\"\n"
        "#  endif\n"
        "#endif\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n\n";

    name = mnpbc_module_name_upper(ctx);
    (void)bytestream_nprintf(bs,
                            1024,
                            "/* autogenerated by mnpbc */\n"
                            "#ifndef %s_H\n"
                            "#define %s_H\n\n",
                            BDATA(name),
                            BDATA(name));
    (void)bytestream_cat(bs, strlen(buf), buf);
    (void)bytestream_nprintf(bs, 1024,
        "#define %s_FNUM(field, ufield) %s_FNUM_ ## field ## _ ## ufield\n"
        "#define %s_MEMBER(cont, field, ufield) ((cont)->field.data.ufield)\n"
        "#define %s_GETFNUM(cont, field) ((cont)->field.fnum)\n"
        "#define %s_SETFNUM(cont, field, ufield) "
        "do { (cont)->field.fnum = %s_FNUM(field, ufield); "
        "} while (0)\n"
        "#define %s_SETDATA(cont, field, ufield, value) "
        "do { %s_MEMBER(cont, field, ufield) = value; "
        "} while (0)\n"
        "#define %s_SET(cont, field, ufield, value) "
        "do { (cont)->field.fnum = %s_FNUM(field, ufield); "
        "%s_MEMBER(cont, field, ufield) = value; } while (0)\n"
        ,
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name),
        BDATA(name));
    BYTES_DECREF(&name);

}


static void
print_header_post(mnpbc_ctx_t *ctx, mnbytestream_t *bs)
{
    mnbytes_t *name;

    name = mnpbc_module_name_upper(ctx);
    (void)bytestream_nprintf(bs,
                             1024,
                             "#ifdef __cplusplus\n"
                             "}\n"
                             "#endif\n"
                             "#endif /* %s_H */\n", BDATA(name));
    BYTES_DECREF(&name);
}

static int
print_forward_decl(UNUSED mnbytes_t *key,
                    mnpbc_container_t *cont,
                    mnbytestream_t *bs)
{
    if (cont->kind == MNPBC_CONT_KBUILTIN) {
        return 0;
    }
    (void)bytestream_nprintf(bs,
                       1024,
                       "%s%s;\n",
                       mnpbc_container_keyword(cont),
                       BDATA(cont->be.fqname));
    return 0;
}


static int
print_field_single(mnpbc_field_t *field, mnbytestream_t *bs, char *indent)
{
    if (field->ty == NULL) {
        /* enum */
        (void)bytestream_nprintf(bs, 1024, "%s", indent);
        (void)bytestream_nprintf(bs,
                           1024,
                           "%s = %"PRId64",\n",
                           BDATA(field->be.name),
                           field->fnum);
    } else {
        mnbytes_t *sig;

        sig = get_field_signature(field);
        if (field->flags.repeated) {
            (void)bytestream_nprintf(bs, 1024, "%s", indent);
            (void)bytestream_nprintf(bs, 1024, "%s;\n", BDATA(sig));

        } else {
            if (field->cty != NULL && field->cty->kind == MNPBC_CONT_KONEOF) {
                mnbytes_t *name;
                mnpbc_field_t **ufield;
                mnarray_iter_t it;

                name = mnpbc_module_name_upper(field->cty->ctx);

                for (ufield = array_first(&field->cty->fields, &it);
                     ufield != NULL;
                     ufield = array_next(&field->cty->fields, &it)) {
                    (void)bytestream_nprintf(bs, 1024,
                        "#define %s_FNUM_%s_%s (%"PRId64")\n",
                        BDATA(name),
                        BDATA(field->be.name),
                        BDATA((*ufield)->be.name),
                        (*ufield)->fnum);
                }

                BYTES_DECREF(&name);
            }

            (void)bytestream_nprintf(bs, 1024, "%s", indent);
            (void)bytestream_nprintf(bs, 1024, "%s; /* %s %s = %"PRId64" */\n",
                                     BDATA(sig),
                                     BDATA(field->ty),
                                     BDATA(field->pb.name),
                                     field->fnum);
        }
        BYTES_DECREF(&sig);
    }

    return 0;
}


static int
print_field_repeated(mnpbc_field_t *field, mnbytestream_t *bs)
{
    (void)bytestream_nprintf(bs,
                            1024,
                            "    struct {\n");
    (void)bytestream_nprintf(bs,
                            1024,
                            "        size_t sz;\n");
    print_field_single(field, bs, "        ");

    (void)bytestream_nprintf(bs,
                            1024,
                            "    } %s; /* repeated %s %s = %"PRId64" */\n",
                            BDATA(field->be.name),
                            BDATA(field->ty),
                            BDATA(field->pb.name),
                            field->fnum);
    return 0;
}


static int
print_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    int res;
    if ((*field)->flags.repeated) {
        res =print_field_repeated(*field, bs);
    } else {
        res = print_field_single(*field, bs, "    ");
    }

    return res;
}


static void
print_decl_recursive(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    mnpbc_container_t **pcont;
    mnpbc_field_t **field;
    mnarray_iter_t it;

    if (cont->flags.visited) {
        return;
    }
    cont->flags.visited = 1;

    for (pcont = array_first(&cont->containers, &it);
         pcont != NULL;
         pcont = array_next(&cont->containers, &it)) {
        assert((*pcont)->kind != MNPBC_CONT_KBUILTIN);
        if ((*pcont)->kind != MNPBC_CONT_KBUILTIN) {
            print_decl_recursive((*pcont), bs);
        }
    }

    for (field = array_first(&cont->fields, &it);
         field != NULL;
         field = array_next(&cont->fields, &it)) {
        if ((*field)->cty != NULL &&
            (*field)->cty->kind != MNPBC_CONT_KBUILTIN) {
            print_decl_recursive((*field)->cty, bs);
        }
    }

    (void)bytestream_nprintf(bs,
                       1024,
                       "%s%s {\n",
                       mnpbc_container_keyword(cont),
                       BDATA(cont->be.fqname));

    if (cont->kind == MNPBC_CONT_KMESSAGE) {
        (void)bytestream_nprintf(bs, 1024, "    ssize_t _mnpbcc_rawsz;\n");
    }

    mnpbc_container_traverse_fields(cont, (array_traverser_t)print_field, bs);
    (void)bytestream_nprintf(bs, 1024, "};\n");
}


static int
print_decl(UNUSED mnbytes_t *key,
            mnpbc_container_t *cont,
            mnbytestream_t *bs)
{
    if (cont->kind == MNPBC_CONT_KBUILTIN) {
        return 0;
    }
    if (cont->parent != NULL) {
        return 0;
    }
    print_decl_recursive(cont, bs);
    return 0;
}


static int
print_alloc_decl_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    if ((*field)->flags.repeated) {
        mnpbc_container_t *cty, *cont;
        char *kwf, *kwc;

        cty = (*field)->cty;
        kwf = mnpbc_container_keyword(cty);

        cont = (*field)->parent;
        kwc = mnpbc_container_keyword(cont);

        (void)bytestream_nprintf(bs, 1024,
            "%s%s *%s_%s_alloc(%s%s *, int);\n",
            kwf == NULL ? "" : kwf,
            BDATA(cty->be.fqname),
            BDATA(cont->be.fqname),
            BDATA((*field)->be.name),
            kwc,
            BDATA(cont->be.fqname));
    }
    return 0;
}


static void
print_alloc_decl(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    mnpbc_container_traverse_fields(cont,
                                     (array_traverser_t)print_alloc_decl_field,
                                     bs);
}


static int
print_method_decl(UNUSED mnbytes_t *key,
                  mnpbc_container_t *cont,
                  mnbytestream_t *bs)
{
    char *kw;

    /*
     * non-builtins messages only
     */
    if (cont->kind != MNPBC_CONT_KMESSAGE) {
        return 0;
    }

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "%s%s *%s_new(void);\n",
                             kw,
                             BDATA(cont->be.fqname),
                             BDATA(cont->be.fqname));
    print_alloc_decl(cont, bs);
    (void)bytestream_nprintf(bs,
                             1024,
                             "int %s_init(%s%s *);\n",
                             BDATA(cont->be.fqname),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t %s("
                             "mnbytestream_t *, %s%s *);\n",
                             BDATA(cont->be.encode),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t %s("
                             "mnbytestream_t *, void *, %s%s *);\n",
                             BDATA(cont->be.decode),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs,
                             1024,
                             "size_t %s(%s%s *);\n",
                             BDATA(cont->be.sz),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t %s(%s%s *, ssize_t);\n",
                             BDATA(cont->be.rawsz),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t %s("
                             "mnbytestream_t *, %s%s *);\n",
                             BDATA(cont->be.dump),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs,
                             1024,
                             "int %s_fini(%s%s *);\n",
                             BDATA(cont->be.fqname),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs,
                             1024,
                             "void %s_destroy(%s%s **);\n\n",
                             BDATA(cont->be.fqname),
                             kw,
                             BDATA(cont->be.fqname));
    return 0;
}


/*
 * Implementation, *.proto.c
 */
static void
print_impl_pre(mnpbc_ctx_t *ctx, mnbytestream_t *bs)
{
    (void)bytestream_nprintf(bs,
                            1024,
                            "/* autogenerated by mnpbc */\n"
                            "#include <limits.h>\n"
                            "#include <stdlib.h>\n"
                            "#include <stdbool.h>\n"
                            "#include <stdint.h>\n"
                            "#include <string.h>\n"
                            "#include <mncommon/dumpm.h>\n"
                            "#include <mncommon/util.h>\n"
                            "#include \"%s\"\n",
                            BDATA(ctx->nameout0));
}


static void
print_new(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "%s%s *\n"
                             "%s_new(void)\n"
                             "{\n",
                             kw,
                             BDATA(cont->be.fqname),
                             BDATA(cont->be.fqname));

    (void)bytestream_nprintf(bs,
                             1024,
                             "    %s%s *res;\n"
                             "    if ((res = malloc(sizeof(%s%s))) != NULL) "
                                 "{ memset(res, 0, sizeof(%s%s)); "
                                 "res->_mnpbcc_rawsz = INT_MAX; }\n"
                             "    return res;\n",
                             kw,
                             BDATA(cont->be.fqname),
                             kw,
                             BDATA(cont->be.fqname),
                             kw,
                             BDATA(cont->be.fqname));

    (void)bytestream_nprintf(bs, 1024, "}\n");

}


static int
print_alloc_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    if ((*field)->flags.repeated) {
        mnpbc_container_t *cty, *cont;
        char *kwf, *kwc;

        cty = (*field)->cty;
        kwf = mnpbc_container_keyword(cty);

        cont = (*field)->parent;
        kwc = mnpbc_container_keyword(cont);

        (void)bytestream_nprintf(bs, 1024,
            "%s%s *\n"
            "%s_%s_alloc(%s%s *msg, int n)\n"
            "{\n"
            "    %s%s*tmp;\n"
            "    if (n > 0) {\n"
            "        if ((tmp = realloc(msg->%s.data, "
                        "sizeof(msg->%s.data[0]) * "
                        "(msg->%s.sz + n))) != NULL) {\n"
            "            msg->%s.data = tmp;\n"
            "            memset(tmp + msg->%s.sz, 0, "
                            "sizeof(msg->%s.data[0]) * n);\n"
            "           tmp = msg->%s.data + msg->%s.sz;\n"
            "           msg->%s.sz += n;\n"
            "       }\n"
            "   } else {\n"
            "       tmp = NULL;\n"
            "   }\n"
            "   return tmp;\n"
            "}\n",
            kwf == NULL ? "" : kwf,
            BDATA(cty->be.fqname),
            BDATA(cont->be.fqname),
            BDATA((*field)->be.name),
            kwc,
            BDATA(cont->be.fqname),
            kwf == NULL ? "" : kwf,
            BDATA(cty->be.fqname),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name),
            BDATA((*field)->be.name));
    }
    return 0;
}


static void
print_alloc(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    mnpbc_container_traverse_fields(cont,
                                     (array_traverser_t)print_alloc_field,
                                     bs);
}


static int
print_fini_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    mnpbc_container_t *cty;

    cty = (*field)->cty;
    if (cty == NULL) {
        assert((*field)->wtype == MNPB_WT_UNDEF);

        (void)bytestream_nprintf(bs, 1024,
            "    //(external:%s) %s\n",
            BDATA((*field)->ty),
            BDATA((*field)->pb.name));

    } else if (bytes_cmp(cty->pb.name, &_bytes) == 0 ||
               bytes_cmp(cty->pb.name, &_string) == 0) {
        /*
         * BYTES_DECREF(&cont->%s);
         */
        if ((*field)->flags.repeated) {
            (void)bytestream_nprintf(bs, 1024,
                "    if (msg->%s.data != NULL) { "
                    "for (size_t i = 0; i < msg->%s.sz; ++i) { "
                        "BYTES_DECREF(&msg->%s.data[i]); "
                    "} "
                    "free(msg->%s.data); "
                    "msg->%s.data = NULL; "
                    "msg->%s.sz = 0; "
                "}\n",
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name));
        } else {
            (void)bytestream_nprintf(bs,
                                     1024,
                                     "    BYTES_DECREF(&msg->%s);\n",
                                     BDATA((*field)->be.name));
        }

    } else if (cty->kind == MNPBC_CONT_KMESSAGE) {
        if ((*field)->flags.repeated) {
            (void)bytestream_nprintf(bs, 1024,
                "    if (msg->%s.data != NULL) { "
                "for (size_t i = 0; i < msg->%s.sz; ++i) { "
                    "%s_fini(&msg->%s.data[i]); "
                "} "
                "free(msg->%s.data); "
                "msg->%s.data = NULL; "
                "msg->%s.sz = 0; "
                "}\n",
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->cty->be.fqname),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name));
        } else {
            (void)bytestream_nprintf(bs,
                                     1024,
                                     "    %s_fini(&msg->%s);\n",
                                     BDATA(cty->be.fqname),
                                     BDATA((*field)->be.name));
        }

    } else if (cty->kind == MNPBC_CONT_KONEOF) {
        mnpbc_field_t **ufield;
        mnarray_iter_t it;

        assert(!(*field)->flags.repeated);
        assert((*field)->wtype == MNPB_WT_INTERN);

        (void)bytestream_nprintf(bs, 1024,
            "    switch (msg->%s.fnum) {\n",
            BDATA((*field)->be.name));

        for (ufield = array_first(&cty->fields, &it);
             ufield != NULL;
             ufield = array_next(&cty->fields, &it)) {
            mnpbc_container_t *ucty;

            assert(!(*ufield)->flags.repeated);

            ucty = (*ufield)->cty;
            if (ucty == NULL) {
                /* extern? */
                continue;
            }
            //assert(ucty->kind == MNPBC_CONT_KBUILTIN);

            if (bytes_cmp(ucty->pb.name, &_bytes) == 0 ||
                bytes_cmp(ucty->pb.name, &_string) == 0) {
                (void)bytestream_nprintf(bs, 1024,
                    "    case %"PRId64": "
                        "BYTES_DECREF(&msg->%s.data.%s); "
                        "break;\n",
                    (*ufield)->fnum,
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name));
            } else if (ucty->kind == MNPBC_CONT_KMESSAGE) {
                (void)bytestream_nprintf(bs, 1024,
                    "    case %"PRId64": "
                        "%s_fini(&msg->%s.data.%s); "
                        "break;\n",
                    (*ufield)->fnum,
                    BDATA(ucty->be.fqname),
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name));
            } else {
                (void)bytestream_nprintf(bs, 1024,
                    "    // case %"PRId64": no fini for %s %s.%s\n",
                    (*ufield)->fnum,
                    BDATA(ucty->pb.name),
                    BDATA((*field)->pb.name),
                    BDATA((*ufield)->pb.name));
            }
        }

        (void)bytestream_nprintf(bs, 1024,
            "    default: break;\n"
            "    }\n");

    } else {
        if ((*field)->flags.repeated) {
            (void)bytestream_nprintf(bs, 1024,
                "    if (msg->%s.data != NULL) { "
                        "free(msg->%s.data); msg->%s.data = NULL; "
                        "msg->%s.sz = 0; "
                        "}\n",
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name),
                BDATA((*field)->be.name));
        } else {
            (void)bytestream_nprintf(bs, 1024,
                "    // no fini for %s %s\n",
                BDATA(cty->pb.name),
                BDATA((*field)->pb.name));
        }
    }

    return 0;
}


static void
print_fini(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    assert(cont->kind == MNPBC_CONT_KMESSAGE);

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "int\n%s_fini(%s%s *msg)\n{\n",
                             BDATA(cont->be.fqname),
                             kw,
                             BDATA(cont->be.fqname));

    mnpbc_container_traverse_fields(cont,
                                     (array_traverser_t)print_fini_field,
                                     bs);

    (void)bytestream_nprintf(bs, 1024, "    return 0;\n}\n");
}


static void
print_destroy(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    assert(cont->kind == MNPBC_CONT_KMESSAGE);

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "void %s_destroy(%s%s **msg)\n"
                             "{\n",
                             BDATA(cont->be.fqname),
                             kw,
                             BDATA(cont->be.fqname));

    (void)bytestream_nprintf(bs, 1024,
                             "    if (*msg != NULL) { "
                                 "%s_fini(*msg); "
                                 "free(*msg); "
                                 "*msg = NULL; "
                             "}\n",
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs, 1024, "}\n");
}


static int
print_pack_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    mnpbc_container_t *cty;

    cty = (*field)->cty;

    if (cty == NULL) {
        assert((*field)->wtype == MNPB_WT_UNDEF);

        (void)bytestream_nprintf(bs, 1024,
            "    //(external:%s) %s\n",
            BDATA((*field)->ty),
            BDATA((*field)->pb.name));

    } else if ((*field)->wtype == MNPB_WT_INTERN) {
        mnpbc_field_t **ufield;
        mnarray_iter_t it;

        /*
         * MNPB_WT_INTERN is not a "valid" wire type.  I use it as
         * a sentinel for oneof fields.
         *
         * oneof: runtime key calculation
         */
        assert(cty->kind == MNPBC_CONT_KONEOF);
        assert(!(*field)->flags.repeated);

        (void)bytestream_nprintf(bs, 1024,
            "    switch (msg->%s.fnum) {\n",
            BDATA((*field)->be.name));

        for (ufield = array_first(&cty->fields, &it);
             ufield != NULL;
             ufield = array_next(&cty->fields, &it)) {
            mnpbc_container_t *ucty;

            assert(!(*ufield)->flags.repeated);
            ucty = (*ufield)->cty;

            if (ucty == NULL) {
                /* external? */
                continue;
            }
            //assert(ucty->kind == MNPBC_CONT_KBUILTIN);

            (void)bytestream_nprintf(bs, 1024,
                "    case %"PRId64":\n",
                (*ufield)->fnum);

            if (ucty->kind == MNPBC_CONT_KMESSAGE) {
                /*
                 * embedded tag: ldelim + fnum
                 */
                (void)bytestream_nprintf(bs, 1024,
                    "        if ((nwritten = mnpb_envarint(bs, "
                            "0x%08"PRIx64")) < 0) { "
                            "res = nwritten; "
                            "goto end; "
                        "} "
                        "res += nwritten;\n",
                    MNPB_MAKEKEY(MNPB_WT_LDELIM, (*ufield)->fnum));

                /*
                 * embedded message, encode as bytes
                 */
                (void)bytestream_nprintf(bs, 1024,
                    "        if ((nwritten = mnpb_envarint(bs, "
                            "%s(%smsg->%s.data.%s))) < 0) { "
                            "res = nwritten; "
                            "goto end; "
                        "} "
                        "res += nwritten;\n",
                    BDATA(ucty->be.sz),
                    ucty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name));

                (void)bytestream_nprintf(bs, 1024,
                    "        if ((nwritten = %s(bs, %smsg->%s.data.%s)) < 0) { "
                        "res = nwritten; "
                        "goto end; "
                    "} "
                    "res += nwritten;\n",
                    BDATA(ucty->be.encode),
                    ucty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name));

            } else {
                (void)bytestream_nprintf(bs, 1024,
                    "        if (msg->%s.data.%s == (%s)%s) { "
                                "break; "
                                "}\n",
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name),
                    BDATA(ucty->be.fqname),
                    MNPB_WT_NUMERIC((*ufield)->wtype) ? "0" : "NULL");

                /*
                 * normal tag: wtype + fnum
                 */
                (void)bytestream_nprintf(bs, 1024,
                    "        if ((nwritten = mnpb_envarint(bs, "
                            "0x%08"PRIx64")) < 0) { "
                            "res = nwritten; "
                            "goto end; "
                        "} "
                        "res += nwritten;\n",
                    MNPB_MAKEKEY((*ufield)->wtype, (*ufield)->fnum));

                (void)bytestream_nprintf(bs, 1024,
                    "        if ((nwritten = %s(bs, %smsg->%s.data.%s)) < 0) { "
                        "res = nwritten; "
                        "goto end; "
                    "} "
                    "res += nwritten;\n",
                    BDATA(ucty->be.encode),
                    ucty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name));

            }

            (void)bytestream_nprintf(bs, 1024,
                "        break;\n");
        }

        (void)bytestream_nprintf(bs, 1024,
            "    default: break;\n"
            "    }\n");

    } else if ((*field)->flags.repeated) {
        /*
         * packed
         */

        /*
         * special tag: ldelim + fnum
         */

        /* sz */
        (void)bytestream_nprintf(bs, 1024,
            "    sz = 0; "
            "for (size_t i = 0; i < msg->%s.sz; ++i) { "
                "sz += %s(%smsg->%s.data[i]); "
            "}\n",
            BDATA((*field)->be.name),
            BDATA(cty->be.sz),
            cty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
            BDATA((*field)->be.name));

        (void)bytestream_nprintf(bs, 1024, "    if (sz > 0) {\n");

        (void)bytestream_nprintf(bs, 1024,
            "        if ((nwritten = mnpb_envarint(bs, 0x%08"PRIx64")) < 0) { "
                     "res = nwritten; "
                     "goto end; "
            "} "
            "res += nwritten;\n",
            MNPB_MAKEKEY(MNPB_WT_LDELIM, (*field)->fnum));

        (void)bytestream_nprintf(bs, 1024,
            "        if ((nwritten = mnpb_envarint(bs, sz)) < 0) { "
                     "res = nwritten; "
                     "goto end; "
            "} res += nwritten;\n");

        /*
         * array
         */
        (void)bytestream_nprintf(bs, 1024,
            "        for (size_t i = 0; i < msg->%s.sz; ++i) { ",
            BDATA((*field)->be.name));
        if (cty->kind == MNPBC_CONT_KMESSAGE) {
            (void)bytestream_nprintf(bs, 1024,
                "if ((nwritten = mnpb_envarint(bs, %s(&msg->%s.data[i]))) < 0) { "
                    "res = nwritten; goto end; "
                "} "
                "res += nwritten; ",
                BDATA(cty->be.sz),
                BDATA((*field)->be.name));
        }

        (void)bytestream_nprintf(bs, 1024,
                     "if ((nwritten = %s(bs, %smsg->%s.data[i])) < 0) { "
                         "res = nwritten; "
                         "goto end; "
                     "} "
                     "res += nwritten; "
            "}\n",
            BDATA(cty->be.encode),
            cty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
            BDATA((*field)->be.name));

        (void)bytestream_nprintf(bs, 1024, "    }\n");

    } else {
        /*
         * normal tag: wtype + fnum
         */
        if (cty->kind != MNPBC_CONT_KMESSAGE) {
            /* builtins and enums */
            (void)bytestream_nprintf(bs, 1024,
                "    if (msg->%s != (%s%s)%s) {\n",
                BDATA((*field)->be.name),
                cty->kind == MNPBC_CONT_KENUM ? "enum " : "",
                BDATA(cty->be.fqname),
                MNPB_WT_NUMERIC((*field)->wtype) ? "0" : "NULL");
        } else {
            (void)bytestream_nprintf(bs, 1024,
                "    if ((sz = %s(&msg->%s)) != 0) {\n",
                BDATA(cty->be.sz),
                BDATA((*field)->be.name));
        }

        (void)bytestream_nprintf(bs, 1024,
            "        if ((nwritten = mnpb_envarint(bs, 0x%08"PRIx64")) < 0) { "
                "res = nwritten; "
                "goto end; "
            "} "
            "res += nwritten;\n",
            MNPB_MAKEKEY((*field)->wtype, (*field)->fnum));

        if (cty->kind == MNPBC_CONT_KMESSAGE) {
            /*
             * embedded message, encode as bytes
             */
            (void)bytestream_nprintf(bs, 1024,
                "        if ((nwritten = mnpb_envarint(bs, sz)) < 0) { "
                    "res = nwritten; "
                    "goto end; "
                "} "
                "res += nwritten;\n");
        }

        (void)bytestream_nprintf(bs, 1024,
            "        if ((nwritten = %s(bs, %smsg->%s)) < 0) { "
                "res = nwritten; "
                "goto end; "
            "} "
            "res += nwritten;\n",
            BDATA(cty->be.encode),
            cty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
            BDATA((*field)->be.name));

        (void)bytestream_nprintf(bs, 1024, "    }\n");
    }

    return 0;
}


static void
print_pack(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    assert(cont->kind == MNPBC_CONT_KMESSAGE);

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t\n"
                             "%s(mnbytestream_t *bs, %s%s *msg)\n"
                             "{\n"
                             "    ssize_t res = 0;\n"
                             "    ssize_t nwritten;\n"
                             "    size_t sz;\n\n",
                             BDATA(cont->be.encode),
                             kw,
                             BDATA(cont->be.fqname));

    mnpbc_container_traverse_fields(cont,
                                     (array_traverser_t)print_pack_field,
                                     bs);

    (void)bytestream_nprintf(bs, 1024,
                             "end:\n"
                             "    return res;\n}"
                             "\n");
}


static int
print_unpack_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    mnpbc_container_t *cty;

    cty = (*field)->cty;

    if (cty == NULL) {
        assert((*field)->wtype == MNPB_WT_UNDEF);

        (void)bytestream_nprintf(bs, 1024,
            "    //(external:%s) %s\n",
            BDATA((*field)->ty),
            BDATA((*field)->pb.name));

    } else if ((*field)->wtype == MNPB_WT_INTERN) {
        mnpbc_field_t **ufield;
        mnpbc_field_t **cfield;
        mnarray_iter_t uit;
        mnarray_iter_t cit;

        /*
         * MNPB_WT_INTERN is not a "valid" wire type.  I use it as
         * a sentinel for oneof fields.
         *
         * oneof: runtime key calculation
         */
        assert(cty->kind == MNPBC_CONT_KONEOF);
        assert(!(*field)->flags.repeated);

        for (ufield = array_first(&cty->fields, &uit);
             ufield != NULL;
             ufield = array_next(&cty->fields, &uit)) {
            mnpbc_container_t *ucty;

            assert(!(*ufield)->flags.repeated);
            ucty = (*ufield)->cty;
            if (ucty == NULL) {
                /* external? */
                continue;
            }
            //assert(ucty->kind == MNPBC_CONT_KBUILTIN);
            //
            //
            //
            //
            //
            //
            //
            (void)bytestream_nprintf(bs, 1024,
                "        case %"PRId64":\n",
                (*ufield)->fnum);

            /* cleanup old messages, strings and/or bytes */
            (void)bytestream_nprintf(bs, 1024,
                "            switch (msg->%s.fnum) {\n",
                BDATA((*field)->be.name));

            for (cfield = array_first(&cty->fields, &cit);
                 cfield != NULL;
                 cfield = array_next(&cty->fields, &cit)) {
                mnpbc_container_t *ccty;

                ccty = (*cfield)->cty;
                if (ccty->kind == MNPBC_CONT_KMESSAGE) {
                    (void)bytestream_nprintf(bs, 1024,
                        "            case %"PRId64": %s_fini(&msg->%s.data.%s); break;\n",
                        (*cfield)->fnum,
                        BDATA(ccty->be.fqname),
                        BDATA((*field)->be.name),
                        BDATA((*cfield)->be.name));
                } else {
                    if (!MNPB_WT_NUMERIC((*cfield)->wtype)) {
                        (void)bytestream_nprintf(bs, 1024,
                            "            case %"PRId64": "
                                "BYTES_DECREF(&msg->%s.data.%s); break;\n",
                            (*cfield)->fnum,
                            BDATA((*field)->be.name),
                            BDATA((*cfield)->be.name));
                    }
                }
            }
            (void)bytestream_nprintf(bs, 1024,
                "            default: break;\n"
                "            }\n");

            /* write new value */
            if (ucty->kind == MNPBC_CONT_KMESSAGE) {
                assert((*ufield)->wtype == MNPB_WT_LDELIM);
                (void)bytestream_nprintf(bs, 1024,
                    "            if (wtype != %d) { res = MNPB_ETYPE; "
                                    "goto end; }\n"
                    "            if ((nread = mnpb_devarint(bs, fd, &sz)) "
                                    "< 0) { res = nread; goto end; } "
                                    "res += nread;\n"
                    "            msg->%s.data.%s._mnpbcc_rawsz = sz;\n"
                    "            if ((nread = %s(bs, fd, &msg->%s.data.%s)) "
                                    "< 0) { res = nread; goto end;}\n"
                    "            // if (nread != (ssize_t)sz) { "
                                    "res = -2; goto end; }\n"
                    "            msg->%s.fnum = %"PRId64"; "
                                    "res += nread; break;\n",
                    (*ufield)->wtype,
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name),
                    BDATA(ucty->be.decode),
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name),
                    BDATA((*field)->be.name),
                    (*ufield)->fnum);

            } else {
                (void)bytestream_nprintf(bs, 1024,
                    "            if ((nread = %s(bs, fd, wtype, "
                                    "&msg->%s.data.%s)) < 0) { "
                                     "res = nread; goto end; }\n"
                    "            msg->%s.fnum = %"PRId64"; "
                                    "res += nread; break;\n",
                    BDATA(ucty->be.decode),
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name),
                    BDATA((*field)->be.name),
                    (*ufield)->fnum);
            }





#if 0
            if (ucty->kind == MNPBC_CONT_KMESSAGE) {
                assert((*ufield)->wtype == MNPB_WT_LDELIM);
                FAIL("print_unpack_field");

            } else if (ucty->kind == MNPBC_CONT_KENUM) {
                FAIL("print_unpack_field");

            } else if (ucty->kind == MNPBC_CONT_KBUILTIN) {
                mnpbc_field_t **cfield;
                mnarray_iter_t cit;

                (void)bytestream_nprintf(bs, 1024,
                    "        case %"PRId64":\n",
                    (*ufield)->fnum);

                /* cleanup old strings and/or bytes */
                (void)bytestream_nprintf(bs, 1024,
                    "            switch (msg->%s.fnum) {\n",
                    BDATA((*field)->be.name));

                for (cfield = array_first(&cty->fields, &cit);
                     cfield != NULL;
                     cfield = array_next(&cty->fields, &cit)) {

                    if (cfield == ufield) {
                        continue;
                    }

                    if (!MNPB_WT_NUMERIC((*cfield)->wtype)) {
                        (void)bytestream_nprintf(bs, 1024,
                            "            case %"PRId64": "
                                "BYTES_DECREF(&msg->%s.data.%s); break;\n",
                            (*cfield)->fnum,
                            BDATA((*field)->be.name),
                            BDATA((*cfield)->be.name));
                    }
                }
                (void)bytestream_nprintf(bs, 1024,
                    "            default: break;\n"
                    "            }\n");

                /* write new value */
                (void)bytestream_nprintf(bs, 1024,
                    "            if ((nread = %s(bs, fd, wtype, "
                                    "&msg->%s.data.%s)) < 0) { "
                                     "res = nread; goto end; }\n"
                    "            msg->%s.fnum = %"PRId64"; "
                                    "res += nread; break;\n",
                    BDATA(ucty->be.decode),
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name),
                    BDATA((*field)->be.name),
                    (*ufield)->fnum);

            } else {
                FAIL("print_unpack_field");
            }
#endif
        }



    } else if ((*field)->flags.repeated) {
        mnpbc_container_t *cont;
        char *kwf;

        /*
         * packed
         */
        assert(cty->kind != MNPBC_CONT_KONEOF);
        assert((*field)->wtype != MNPB_WT_INTERN);
        /*
         * wither enum or message or builtin
         */
        kwf = mnpbc_container_keyword(cty);

        cont = (*field)->parent;
        assert(cont!= NULL);

        (void)bytestream_nprintf(bs, 1024,
            "        case %"PRId64":\n"
            "            if (wtype != %d) { res = MNPB_ETYPE; goto end; }\n"
            "            if ((nread = mnpb_devarint(bs, fd, &sz)) < 0) { "
                            "res = nread; goto end; } res += nread;\n"
            "            for (nread_item = 0, nread = 0; "
                                "nread_item < (ssize_t)sz; ) {\n"
            "                %s%s *item;\n"
            "                //uint64_t etag;\n"
            "                uint64_t esz;\n"
            "                if ((item = %s_%s_alloc(msg, 1)) == NULL) { "
                                "res = MNPB_EMEMORY; goto end; }\n"
            ,
            (*field)->fnum,
            MNPB_WT_LDELIM,
            kwf == NULL ? "" : kwf,
            BDATA(cty->be.fqname),
            BDATA(cont->be.fqname),
            BDATA((*field)->be.name));

        if (cty->kind == MNPBC_CONT_KMESSAGE) {
            (void)bytestream_nprintf(bs, 1024,
                "                if ((nread = mnpb_devarint(bs, fd, "
                                    "&esz)) < 0) { "
                                    "res = nread; goto end; } "
                                    "item->_mnpbcc_rawsz = esz; "
                                    "nread_item += nread;\n"
                );
            (void)bytestream_nprintf(bs, 1024,
                "                if ((nread = %s(bs, fd, item)) < 0) { "
                                    "res = nread; goto end; } "
                                    "nread_item += nread;\n",
                BDATA(cty->be.decode));

        } else if (cty->kind == MNPBC_CONT_KENUM) {
            (void)bytestream_nprintf(bs, 1024,
                "                { int64_t v; if ((nread = "
                                    "%s(bs, fd, -1, &v)) < 0) { "
                                    "res = nread; goto end; } *item = v; "
                                    "nread_item += nread; }\n",
                BDATA(cty->be.decode));

        } else if (cty->kind == MNPBC_CONT_KBUILTIN) {
            (void)bytestream_nprintf(bs, 1024,
                "                if ((nread = %s(bs, fd, -1, item)) < 0) { "
                                    "res = nread; goto end; } "
                                    "nread_item += nread;\n",
                BDATA(cty->be.decode));

        } else {
            FAIL("print_unpack_field");
        }

        (void)bytestream_nprintf(bs, 1024,
            "            }\n"
            "            res += nread_item; break;\n"
            );

    } else {
        if (cty->kind == MNPBC_CONT_KMESSAGE) {
            assert((*field)->wtype == MNPB_WT_LDELIM);
            (void)bytestream_nprintf(bs, 1024,
                "        case %"PRId64":\n"
                "            if (wtype != %d) { res = MNPB_ETYPE; goto end; }\n"
                "            if ((nread = mnpb_devarint(bs, fd, &sz)) < 0) { "
                                "res = nread; goto end; } res += nread;\n"
                "            msg->%s._mnpbcc_rawsz = (ssize_t)sz;\n"
                "            if ((nread = %s(bs, fd, &msg->%s)) < 0) { "
                                "res = nread; goto end; }\n"
                "            // if (nread != (ssize_t)sz) { "
                                "res = -2; goto end; }\n"
                "            res += nread; break;\n"
                ,
                (*field)->fnum,
                (*field)->wtype,
                BDATA((*field)->be.name),
                BDATA(cty->be.decode),
                BDATA((*field)->be.name));

        } else if (cty->kind == MNPBC_CONT_KENUM) {
            (void)bytestream_nprintf(bs, 1024,
                "        case %"PRId64":\n"
                "            { int64_t v; "
                                "if ((nread = %s(bs, fd, wtype, &v)) < 0) { "
                                     "res = nread; goto end; "
                                "} "
                                "msg->%s = v; "
                            "}\n"
                "            res += nread; break;\n",
                (*field)->fnum,
                BDATA(cty->be.decode),
                BDATA((*field)->be.name));
        } else if (cty->kind == MNPBC_CONT_KBUILTIN) {
            /*
             * normal tag: wtype + fnum
             */
            (void)bytestream_nprintf(bs, 1024,
                "        case %"PRId64":\n"
                "            if ((nread = %s(bs, fd, wtype, &msg->%s)) < 0) { "
                                 "res = nread; goto end; }\n"
                "            res += nread; break;\n",
                (*field)->fnum,
                BDATA(cty->be.decode),
                BDATA((*field)->be.name));

        } else {
            FAIL("print_unpack_field");
        }
    }

//end:
    return 0;
}


static void
print_unpack(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    assert(cont->kind == MNPBC_CONT_KMESSAGE);

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t\n"
                             "%s(mnbytestream_t *bs, void *fd, "
                             "%s%s *msg)\n{\n"
                             "    ssize_t res = 0;\n"
                             "    ssize_t nread = 0;\n"
                             "    ssize_t nread_item;\n"
                             "    uint64_t sz;\n"
                             "    while (res < msg->_mnpbcc_rawsz) {\n"
                             "        uint64_t tag;\n"
                             "        int wtype;\n"
                             "        if ((nread = mnpb_unpack_key("
                                          "bs, fd, &tag, &wtype)) < 0) { "
                                          "res = nread; goto end; }\n"
                             "        res += nread;\n"
                             "        switch (tag) {\n"
                             ,
                             BDATA(cont->be.decode),
                             kw,
                             BDATA(cont->be.fqname));

    mnpbc_container_traverse_fields(cont,
                                     (array_traverser_t)print_unpack_field,
                                     bs);

    (void)bytestream_nprintf(bs, 1024,
                             "        default:\n"
                             "            if ((nread = mnpb_devoid(bs, fd, "
                                            "tag, wtype)) < 0) { "
                                            "res = nread; goto end; "
                                            "} res += nread; break;\n"
                             "        }\n"
                             "    }\n"
                             "end:\n"
                             "    return res;\n}\n");
}


static int
print_sz_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    mnpbc_container_t *cty;

    cty = (*field)->cty;

    if (cty == NULL) {
        assert((*field)->wtype == MNPB_WT_UNDEF);

        (void)bytestream_nprintf(bs, 1024,
            "    //(external:%s) %s\n",
            BDATA((*field)->ty),
            BDATA((*field)->pb.name));

    } else if ((*field)->wtype == MNPB_WT_INTERN) {
        mnpbc_field_t **ufield;
        mnarray_iter_t it;

        /*
         * MNPB_WT_INTERN is not a "valid" wire type.  I use it as
         * a sentinel for oneof fields.
         *
         * oneof: runtime key calculation
         */
        assert(cty->kind == MNPBC_CONT_KONEOF);
        assert(!(*field)->flags.repeated);

        (void)bytestream_nprintf(bs, 1024,
            "    switch (msg->%s.fnum) {\n",
            BDATA((*field)->be.name));

        for (ufield = array_first(&cty->fields, &it);
             ufield != NULL;
             ufield = array_next(&cty->fields, &it)) {
            mnpbc_container_t *ucty;

            assert(!(*ufield)->flags.repeated);

            ucty = (*ufield)->cty;
            if (ucty == NULL) {
                /* external? */
                continue;
            }
            //assert(ucty->kind == MNPBC_CONT_KBUILTIN);

            (void)bytestream_nprintf(bs, 1024,
                "    case %"PRId64":\n",
                (*ufield)->fnum);

            if (ucty->kind == MNPBC_CONT_KMESSAGE) {
                (void)bytestream_nprintf(bs, 1024,
                    "        res += mnpb_szvarint(0x%08"PRId64") + "
                                "%s(&msg->%s.data.%s); break;\n",
                    MNPB_MAKEKEY(MNPB_WT_LDELIM, (*ufield)->fnum),
                    BDATA(ucty->be.sz),
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name));

            } else {
                (void)bytestream_nprintf(bs, 1024,
                    "        if (msg->%s.data.%s != (%s)%s) { "
                                "res += mnpb_szvarint(0x%08"PRId64") + "
                                "%s(msg->%s.data.%s); } break;\n",
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name),
                    BDATA(ucty->be.fqname),
                    MNPB_WT_NUMERIC((*ufield)->wtype) ? "0" : "NULL",
                    MNPB_MAKEKEY((*ufield)->wtype, (*ufield)->fnum),
                    BDATA(ucty->be.sz),
                    BDATA((*field)->be.name),
                    BDATA((*ufield)->be.name));
            }

        }

        (void)bytestream_nprintf(bs, 1024,
            "    default: break;\n"
            "    }\n");

    } else if ((*field)->flags.repeated) {
        (void)bytestream_nprintf(bs, 1024,
            "    n = 0;\n");

        (void)bytestream_nprintf(bs, 1024,
            "    for (size_t i = 0; i < msg->%s.sz; ++i) { "
            "n += %s(%smsg->%s.data[i]); "
            "}\n",
            BDATA((*field)->be.name),
            BDATA(cty->be.sz),
            cty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
            BDATA((*field)->be.name));

        (void)bytestream_nprintf(bs, 1024,
            "    if (n > 0) { "
            "res += mnpb_szvarint(0x%08"PRIx64") + mnpb_szvarint(n) + n; }\n",
            MNPB_MAKEKEY(MNPB_WT_LDELIM, (*field)->fnum));

    } else {
        if ((cty)->kind == MNPBC_CONT_KMESSAGE) {
            assert((*field)->wtype == MNPB_WT_LDELIM);
            (void)bytestream_nprintf(bs, 1024,
                "    if ((n = %s(&msg->%s)) > 0) { "
                "res += mnpb_szvarint(0x%08"PRIx64") + n; "
                "}\n",
                BDATA(cty->be.sz),
                BDATA((*field)->be.name),
                MNPB_MAKEKEY((*field)->wtype, (*field)->fnum)
                );

        } else if ((*field)->wtype == MNPB_WT_LDELIM) {
            (void)bytestream_nprintf(bs, 1024,
                "    if (msg->%s != NULL) { "
                "res += mnpb_szvarint(0x%08"PRIx64") + %s(msg->%s); "
                "}\n",
                BDATA((*field)->be.name),
                MNPB_MAKEKEY((*field)->wtype, (*field)->fnum),
                BDATA(cty->be.sz),
                BDATA((*field)->be.name));

        } else if (MNPB_WT_NUMERIC((*field)->wtype)) {
            (void)bytestream_nprintf(bs, 1024,
                "    if (msg->%s != 0) { "
                "res += mnpb_szvarint(0x%08"PRIx64") + %s(msg->%s); "
                "}\n",
                BDATA((*field)->be.name),
                MNPB_MAKEKEY((*field)->wtype, (*field)->fnum),
                BDATA(cty->be.sz),
                BDATA((*field)->be.name));

        } else {
            FAIL("print_sz_field");
        }
    }

    return 0;
}


static void
print_sz(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    assert(cont->kind == MNPBC_CONT_KMESSAGE);

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "size_t\n"
                             "%s(%s%s *msg)\n{\n"
                             "    ssize_t res = 0;\n"
                             "    ssize_t n;\n",
                             BDATA(cont->be.sz),
                             kw,
                             BDATA(cont->be.fqname));

    mnpbc_container_traverse_fields(cont,
                                     (array_traverser_t)print_sz_field,
                                     bs);

    (void)bytestream_nprintf(bs, 1024, "    return res;\n}\n");
}


static void
print_rawsz(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    assert(cont->kind == MNPBC_CONT_KMESSAGE);

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t\n"
                             "%s(%s%s *msg, ssize_t rawsz)\n{\n"
                             "    msg->_mnpbcc_rawsz = rawsz;\n"
                             "    return rawsz;\n"
                             "}\n",
                             BDATA(cont->be.rawsz),
                             kw,
                             BDATA(cont->be.fqname));

}


static int
print_dump_field(mnpbc_field_t **field, mnbytestream_t *bs)
{
    mnpbc_container_t *cty;

    (void)bytestream_nprintf(bs, 1024,
        "    res += bytestream_nprintf(bs, 1024, \"%"PRId64":%s:%s=\");\n",
        (*field)->fnum,
        MNPB_WT_CHAR((*field)->wtype),
        BDATA((*field)->pb.name));

    cty = (*field)->cty;

    if (cty == NULL) {
        assert((*field)->wtype == MNPB_WT_UNDEF);

        (void)bytestream_nprintf(bs, 1024,
            "    res += bytestream_nprintf(bs, 1024, "
            "\"<%s %s>\");\n",
            BDATA((*field)->ty),
            BDATA((*field)->pb.name));
    } else if ((*field)->wtype == MNPB_WT_INTERN) {
        mnpbc_field_t **ufield;
        mnarray_iter_t it;

        /*
         * MNPB_WT_INTERN is not a "valid" wire type.  I use it as
         * a sentinel for oneof fields.
         *
         * oneof: runtime key calculation
         */
        assert(cty->kind == MNPBC_CONT_KONEOF);
        assert(!(*field)->flags.repeated);

        (void)bytestream_nprintf(bs, 1024,
            "    switch (msg->%s.fnum) {\n",
            BDATA((*field)->be.name));

        for (ufield = array_first(&cty->fields, &it);
             ufield != NULL;
             ufield = array_next(&cty->fields, &it)) {
            mnpbc_container_t *ucty;

            assert(!(*ufield)->flags.repeated);
            ucty = (*ufield)->cty;
            if (ucty == NULL) {
                /* external? */
                continue;
            }
            //assert(ucty->kind == MNPBC_CONT_KBUILTIN);

            //if (ucty->kind == MNPBC_CONT_KMESSAGE) {
            //    (void)bytestream_nprintf(bs, 1024,
            //        "    case %"PRId64":\n"
            //        "        res += bytestream_nprintf(bs, 1024, \"%"PRId64":%s:%s\");\n"
            //        ,
            //        (*ufield)->fnum,
            //        (*ufield)->fnum,
            //        MNPB_WT_CHAR((*ufield)->wtype),
            //        BDATA((*ufield)->be.name),
            //        );
            //} else {
            //}
            (void)bytestream_nprintf(bs, 1024,
                "    case %"PRId64":\n"
                "        res += bytestream_nprintf(bs, 1024, "
                            "\"%"PRId64":%s:%s=\");\n"
                "        res += %s(bs, %smsg->%s.data.%s);\n"
                "        break;\n",
                (*ufield)->fnum,
                (*ufield)->fnum,
                MNPB_WT_CHAR((*ufield)->wtype),
                BDATA((*ufield)->be.name),
                BDATA(ucty->be.dump),
                ucty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
                BDATA((*field)->be.name),
                BDATA((*ufield)->be.name));
        }

        (void)bytestream_nprintf(bs, 1024,
            "    default: break;\n"
            "    }\n");


    } else if ((*field)->flags.repeated) {
        (void)bytestream_nprintf(bs, 1024,
            "    res += bytestream_cat(bs, 2, \"[ \");\n");

        (void)bytestream_nprintf(bs, 1024,
            "    for (size_t i = 0; i < msg->%s.sz; ++i) { "
                "res += %s(bs, %smsg->%s.data[i]); "
                "res += bytestream_cat(bs, 1, \" \"); "
            "}\n",
            BDATA((*field)->be.name),
            BDATA(cty->be.dump),
            cty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
            BDATA((*field)->be.name));

        (void)bytestream_nprintf(bs, 1024,
            "    res += bytestream_cat(bs, 2, \"] \");\n");
    } else {
        (void)bytestream_nprintf(bs, 1024,
            "    res += %s(bs, %smsg->%s);\n",
            BDATA(cty->be.dump),
            cty->kind == MNPBC_CONT_KMESSAGE ? "&" : "",
            BDATA((*field)->be.name));
    }

    (void)bytestream_nprintf(bs, 1024,
            "    res += bytestream_cat(bs, 1, \" \");\n");

    return 0;
}


static void
print_dump(mnpbc_container_t *cont, mnbytestream_t *bs)
{
    char *kw;

    assert(cont->kind == MNPBC_CONT_KMESSAGE);

    kw = mnpbc_container_keyword(cont);

    (void)bytestream_nprintf(bs,
                             1024,
                             "ssize_t\n"
                             "%s(mnbytestream_t *bs, %s%s *msg)\n{\n"
                             "    ssize_t res = 0;\n",
                             BDATA(cont->be.dump),
                             kw,
                             BDATA(cont->be.fqname));
    (void)bytestream_nprintf(bs, 1024,
        "    res += bytestream_cat(bs, 2, \"{ \");\n");

    mnpbc_container_traverse_fields(cont,
                                     (array_traverser_t)print_dump_field,
                                     bs);
    (void)bytestream_nprintf(bs, 1024,
        "    res += bytestream_cat(bs, 3, \"} \");\n");

    (void)bytestream_nprintf(bs, 1024,
                             "    SADVANCEEOD(bs, -1);\n"
                             "    return res;\n}\n");
}


static int
print_method_def(UNUSED mnbytes_t *key,
                 mnpbc_container_t *cont,
                 mnbytestream_t *bs)
{
    if (cont->kind != MNPBC_CONT_KMESSAGE) {
        return 0;
    }

    print_new(cont, bs);
    print_alloc(cont, bs);
    // print_init(cont, bs);
    print_fini(cont, bs);
    print_destroy(cont, bs);
    print_pack(cont, bs);
    print_unpack(cont, bs);
    print_sz(cont, bs);
    print_rawsz(cont, bs);
    print_dump(cont, bs);

    return 0;
}


/*
 * Analyzer
 */
static int
analyze_backend3(mnpbc_field_t **field, UNUSED void *udata)
{
    /*
     * analyze field
     */
    assert((*field)->be.name == NULL);

    (*field)->be.name = (*field)->pb.name;
    BYTES_INCREF((*field)->be.name);

    assert((*field)->be.fqname == NULL);
    assert((*field)->parent != NULL);
    assert((*field)->parent->be.fqname != NULL);

    (*field)->be.fqname = bytes_printf("%s.%s",
                                       BDATA((*field)->parent->be.fqname),
                                       BDATA((*field)->be.name));
    BYTES_INCREF((*field)->be.fqname);
    if ((*field)->cty != NULL) {
        (*field)->wtype = mnpbc_wtype_from_type((*field)->cty);
    }

    return 0;
}


static void
set_be_methods(mnpbc_container_t *cont)
{
    /*
     * non-builtin methods
     */
    if (cont->kind == MNPBC_CONT_KENUM) {
        mnpbc_container_set_be_encode(cont,
                                       bytes_printf("mnpb_envarint"));
        mnpbc_container_set_be_decode(cont,
                                       bytes_printf("mnpb_unpack_int64"));
        mnpbc_container_set_be_sz(cont,
                                   bytes_printf("mnpb_szvarint"));
        mnpbc_container_set_be_dump(cont,
                                     bytes_printf("mnpb_dumpvarint"));

    } else if (cont->kind == MNPBC_CONT_KMESSAGE ||
               cont->kind == MNPBC_CONT_KONEOF) {
        mnpbc_container_set_be_encode(
            cont, bytes_printf("%s_pack", BDATA(cont->be.fqname)));
        mnpbc_container_set_be_decode(
            cont, bytes_printf("%s_unpack", BDATA(cont->be.fqname)));
        mnpbc_container_set_be_sz(
            cont, bytes_printf("%s_sz", BDATA(cont->be.fqname)));
        mnpbc_container_set_be_rawsz(
            cont, bytes_printf("%s_rawsz", BDATA(cont->be.fqname)));
        mnpbc_container_set_be_dump(
            cont, bytes_printf("%s_dump", BDATA(cont->be.fqname)));

    } else {
        FAIL("set_be_methods");
    }
}


static int
analyze_backend2(mnpbc_container_t **cont, UNUSED void *udata)
{
    /*
     * lower-level containers only
     */
    assert((*cont)->parent != NULL);
    assert((*cont)->parent->be.fqname != NULL);
    analyze_backend1(*cont);
    return 0;
}


static void
analyze_backend1(mnpbc_container_t *cont)
{
    /*
     * analyze container
     */
    if (cont->flags.visited) {
        return;
    }
    cont->flags.visited = 1;

    assert(cont->be.name == NULL);
    cont->be.name = cont->pb.name;
    BYTES_INCREF(cont->be.name);

    assert(cont->be.fqname == NULL);
    if (cont->parent == NULL) {
        cont->be.fqname = cont->pb.fqname;
    } else {
        cont->be.fqname = bytes_printf("%s_%s",
                                       BDATA(cont->parent->be.fqname),
                                       BDATA(cont->be.name));
    }
    BYTES_INCREF(cont->be.fqname);

    if (cont->kind != MNPBC_CONT_KBUILTIN) {
        set_be_methods(cont);
        mnpbc_container_traverse_containers(
            cont, (array_traverser_t)analyze_backend2, NULL);
        mnpbc_container_traverse_fields(
            cont, (array_traverser_t)analyze_backend3, NULL);
    }
}


static int
analyze_backend0(UNUSED mnbytes_t *key, mnpbc_container_t *cont, UNUSED void *udata)
{
    /*
     * top-level containers only
     */
    if (cont->parent == NULL) {
        analyze_backend1(cont);
    }
    return 0;
}


void
mnpbc_ctx_render_c(mnpbc_ctx_t *ctx)
{
    mnbytestream_t bs;

    /* analyze */
    (void)mnpbc_ctx_traverse(ctx, (hash_traverser_t)analyze_backend0, NULL);

    bytestream_init(&bs, 1024);

    /* header */
    print_header_pre(ctx, &bs);
    (void)mnpbc_ctx_traverse(ctx, (hash_traverser_t)print_forward_decl, &bs);
    (void)bytestream_nprintf(&bs, 1024, "\n");
    (void)mnpbc_ctx_traverse(ctx, (hash_traverser_t)print_decl, &bs);
    (void)mnpbc_ctx_traverse(ctx, (hash_traverser_t)print_method_decl, &bs);
    print_header_post(ctx, &bs);

    bs.write = bytestream_write;
    bytestream_produce_data(&bs, (void *)(intptr_t)fileno(ctx->out0));

    /* implementation */
    bytestream_rewind(&bs);

    print_impl_pre(ctx, &bs);
    (void)mnpbc_ctx_traverse(ctx, (hash_traverser_t)print_method_def, &bs);

    bytestream_produce_data(&bs, (void *)(intptr_t)fileno(ctx->out1));

    bytestream_fini(&bs);
}


void
mnpbc_ctx_init_c(mnpbc_ctx_t *ctx,
                  mnbytes_t *namein,
                  FILE *in,
                  mnbytes_t *nameout0,
                  FILE *out0,
                  mnbytes_t *nameout1,
                  FILE *out1)
{
    struct {
        const char *pbname;
        const char *fqname;
        const char *encode;
        const char *decode;
        const char *sz;
        const char *dump;
        int (*print_sz_field)(mnpbc_field_t *, mnbytestream_t *);
    } builtins[] = {
        {"float", "float",
         "mnpb_enfloat",
         "mnpb_unpack_float",
         "mnpb_szfloat",
         "mnpb_dumpfloat",
         NULL,
        },
        {"double", "double",
         "mnpb_endouble",
         "mnpb_unpack_double",
         "mnpb_szdouble",
         "mnpb_dumpdouble",
         NULL,
        },
        {"int32", "int32_t",
         "mnpb_pack_int32",
         "mnpb_unpack_int32",
         "mnpb_sz_int32",
         "mnpb_dumpvarint",
         NULL,
        },
        {"int64", "int64_t",
         "mnpb_envarint",
         "mnpb_unpack_int64",
         "mnpb_szvarint",
         "mnpb_dumpvarint",
         NULL,
        },
        {"uint32", "uint32_t",
         "mnpb_envarint",
         "mnpb_unpack_uint32",
         "mnpb_szvarint",
         "mnpb_dumpvarint",
         NULL,
        },
        {"uint64", "uint64_t",
         "mnpb_envarint",
         "mnpb_unpack_uint64",
         "mnpb_szvarint",
         "mnpb_dumpvarint",
         NULL,
        },
        {"sint32", "int32_t",
         "mnpb_enzz32",
         "mnpb_unpack_sint32",
         "mnpb_szzz32",
         "mnpb_dumpzz32",
         NULL,
        },
        {"sint64", "int64_t",
         "mnpb_enzz64",
         "mnpb_unpack_sint64",
         "mnpb_szzz64",
         "mnpb_dumpzz64",
         NULL,
        },
        {"fixed32", "uint32_t",
         "mnpb_enfi32",
         "mnpb_unpack_fixed32",
         "mnpb_szfi32",
         "mnpb_dumpfi32",
         NULL,
        },
        {"fixed64", "uint64_t",
         "mnpb_enfi64",
         "mnpb_unpack_fixed64",
         "mnpb_szfi64",
         "mnpb_dumpfi64",
         NULL,
        },
        {"sfixed32", "int32_t",
         "mnpb_enfi32",
         "mnpb_unpack_sfixed32",
         "mnpb_szfi32",
         "mnpb_dumpfi32",
         NULL,
        },
        {"sfixed64", "int64_t",
         "mnpb_enfi64",
         "mnpb_unpack_sfixed64",
         "mnpb_szfi64",
         "mnpb_dumpfi64",
         NULL,
        },
        {"bool", "bool",
         "mnpb_envarint",
         "mnpb_unpack_bool",
         "mnpb_szvarint",
         "mnpb_dumpvarint",
         NULL,
        },
        {"string", "mnbytes_t *",
         "mnpb_enstr",
         "mnpb_unpack_string",
         "mnpb_szstr",
         "mnpb_dumpstr",
         NULL,
        },
        {"bytes", "mnbytes_t *",
         "mnpb_enbytes",
         "mnpb_unpack_bytes",
         "mnpb_szbytes",
         "mnpb_dumpbytes",
         NULL,
        },
    };
    unsigned i;

    for (i = 0; i < countof(builtins); ++i) {
        mnpbc_container_t *cont;
        mnbytes_t *ty;
        ty = bytes_new_from_str(builtins[i].pbname);
        cont = mnpbc_ctx_add_container(ctx, NULL, ty, MNPBC_CONT_KBUILTIN);
        mnpbc_container_set_pb_fqname(cont,
                                       bytes_new_from_str(builtins[i].fqname));
        mnpbc_container_set_be_encode(cont,
                                       bytes_new_from_str(builtins[i].encode));
        mnpbc_container_set_be_decode(cont,
                                       bytes_new_from_str(builtins[i].decode));
        mnpbc_container_set_be_sz(cont,
                                   bytes_new_from_str(builtins[i].sz));
        mnpbc_container_set_be_dump(cont,
                                     bytes_new_from_str(builtins[i].dump));
    }

    ctx->namein = namein;
    BYTES_INCREF(ctx->namein);
    if (nameout0 != NULL) {
        ctx->nameout0 = nameout0;
        BYTES_INCREF(ctx->nameout0);
    }
    ctx->nameout1 = nameout1;
    BYTES_INCREF(ctx->nameout1);

    ctx->in = in;
    ctx->out0 = out0;
    ctx->out1 = out1;
}
