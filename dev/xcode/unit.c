/*
 * Copyright (C) 2018 Alexander Borisov
 *
 * Author: Alexander Borisov <lex.borisov@gmail.com>
 */

#include <lexbor/core/fs.h>

#include <lexbor/html/tag.h>
#include <lexbor/html/ns.h>
#include <lexbor/html/parser.h>
#include <lexbor/html/serialize.h>
#include <lexbor/html/interfaces/document.h>

#include <unit/test.h>
#include <unit/kv.h>


typedef struct {
    lxb_html_tag_id_t tag_id;
    lxb_html_ns_id_t  ns;
}
fragment_entry_t;


static lxb_status_t
parse(const char *dir_path);

static lexbor_action_t
file_callback(const lxb_char_t *fullpath, size_t fullpath_len,
              const lxb_char_t *filename, size_t filename_len, void *ctx);

static void
check_entries(unit_kv_t *kv, unit_kv_value_t *value, lxb_html_parser_t *parser);

static void
check_entry(unit_kv_t *kv, unit_kv_value_t *value, lxb_html_parser_t *parser);

static void
check_compare(unit_kv_t *kv, lxb_dom_node_t *root,
              lexbor_str_t *data, lexbor_str_t *result);

static lxb_status_t
serializer_callback(const lxb_char_t *data, size_t len, void *ctx);

static lexbor_str_t *
hash_get_str(unit_kv_t *kv, unit_kv_value_t *hash, const char *name);

static fragment_entry_t
hash_get_fragment(unit_kv_t *kv, unit_kv_value_t *hash);

static bool
hash_get_scripting(unit_kv_t *kv, unit_kv_value_t *hash);

static void
print_error(unit_kv_t *kv, unit_kv_value_t *value);


static lxb_status_t
parse(const char *dir_path)
{
    lxb_status_t status;

    status = lexbor_fs_dir_read((const lxb_char_t *) dir_path,
                                LEXBOR_FS_DIR_OPT_WITHOUT_DIR
                                |LEXBOR_FS_DIR_OPT_WITHOUT_HIDDEN,
                                file_callback, NULL);

    if (status != LXB_STATUS_OK) {
        TEST_PRINTLN("Failed to read directory: %s", dir_path);
    }

    return status;
}

static lexbor_action_t
file_callback(const lxb_char_t *fullpath, size_t fullpath_len,
              const lxb_char_t *filename, size_t filename_len, void *ctx)
{
    unit_kv_t *kv;
    lxb_status_t status;
    unit_kv_value_t *arr_value;

    if (filename_len < 5 ||
        strncmp((const char *) &filename[ (filename_len - 4) ], ".ton", 4) != 0)
    {
        return LEXBOR_ACTION_OK;
    }

    TEST_PRINTLN("Test file: %s", fullpath);

    kv = unit_kv_create();
    status = unit_kv_init(kv, 256);

    if (status != LXB_STATUS_OK) {
        TEST_FAILURE("Failed to create KV parser");
    }

    status = unit_kv_parse_file(kv, fullpath);
    if (status != LXB_STATUS_OK) {
        lexbor_str_t str = unit_kv_parse_error_as_string(kv);

        TEST_PRINTLN("Failed to parse test file:");
        TEST_PRINTLN("%s", str.data);

        unit_kv_string_destroy(kv, &str, false);

        exit(EXIT_FAILURE);
    }

    arr_value = unit_kv_value(kv);
    if (arr_value == NULL) {
        TEST_PRINTLN("File is empty");
    }

    if (unit_kv_is_array(arr_value) == false) {
        TEST_PRINTLN("Error: Need array, but we have:");

        print_error(kv, arr_value);

        exit(EXIT_FAILURE);
    }

    check_entries(kv, arr_value, ctx);

    return LEXBOR_ACTION_OK;
}

static void
check_entries(unit_kv_t *kv, unit_kv_value_t *value, lxb_html_parser_t *parser)
{
    unit_kv_array_t *entries = unit_kv_array(value);

    if (parser != NULL) {
        for (size_t i = 0; i < entries->length; i++) {
            TEST_PRINTLN("\tTest number: "LEXBOR_FORMAT_Z, (i + 1));

            if (unit_kv_is_hash(entries->list[i]) == false) {
                TEST_PRINTLN("Error: Need hash (object), but we have:");

                print_error(kv, entries->list[i]);

                exit(EXIT_FAILURE);
            }

            check_entry(kv, entries->list[i], parser);
        }

        return;
    }

    lxb_status_t status;

    for (size_t i = 0; i < entries->length; i++) {
        TEST_PRINTLN("\tTest number: "LEXBOR_FORMAT_Z, (i + 1));

        if (unit_kv_is_hash(entries->list[i]) == false) {
            TEST_PRINTLN("Error: Need hash (object), but we have:");

            print_error(kv, entries->list[i]);

            exit(EXIT_FAILURE);
        }

        parser = lxb_html_parser_create();
        status = lxb_html_parser_init(parser);

        if (status != LXB_STATUS_OK) {
            TEST_FAILURE("Failed to create parser");
        }

        check_entry(kv, entries->list[i], parser);

        lxb_html_parser_destroy(parser, true);
    }
}

static void
check_entry(unit_kv_t *kv, unit_kv_value_t *hash, lxb_html_parser_t *parser)
{
    lxb_dom_node_t *root;
    lxb_html_document_t *document;

    bool scripting = hash_get_scripting(kv, hash);

    lexbor_str_t *data = hash_get_str(kv, hash, "data");
    lexbor_str_t *result = hash_get_str(kv, hash, "result");
    fragment_entry_t fragment = hash_get_fragment(kv, hash);

    lxb_html_tree_scripting_set(parser->tree, scripting);

    if (fragment.tag_id != LXB_HTML_TAG__UNDEF) {
        root = lxb_html_parse_fragment_by_tag_id(parser, NULL,
                                                 fragment.tag_id, fragment.ns,
                                                 data->data, data->length);
        if (root == NULL) {
            TEST_PRINTLN("Failed to parse fragment data:");
            TEST_FAILURE("%s", data->data);
        }
    }
    else {
        document = lxb_html_parse(parser, data->data, data->length);
        if (document == NULL) {
            TEST_PRINTLN("Failed to parse data:");
            TEST_FAILURE("%s", data->data);
        }

        root = lxb_dom_interface_node(document);
    }

    check_compare(kv, root, data, result);
}

static void
check_compare(unit_kv_t *kv, lxb_dom_node_t *root,
              lexbor_str_t *data, lexbor_str_t *want)
{
    lxb_status_t status;
    lexbor_str_t res = {0};

    status = lxb_html_serialize_pretty_tree_str(root,
                                                LXB_HTML_SERIALIZE_OPT_WITHOUT_CLOSING
                                                |LXB_HTML_SERIALIZE_OPT_RAW
                                                |LXB_HTML_SERIALIZE_OPT_TAG_WITH_NS
                                                |LXB_HTML_SERIALIZE_OPT_WITHOUT_TEXT_INDENT
                                                |LXB_HTML_SERIALIZE_OPT_FULL_DOCTYPE,
                                                0, &res);
    if (status != LXB_STATUS_OK) {
        TEST_FAILURE("Failed to serialization tree");
    }

    /* Skip last \n */
    if (res.length != 0) {
        res.length--;
        res.data[ res.length ] = 0x00;
    }

    if (res.length == want->length
        && lexbor_str_data_cmp(res.data, want->data))
    {
        return;
    }

    TEST_PRINTLN("Error: The result does not match.");
    TEST_PRINTLN("Data:");
    TEST_PRINTLN("%s", data->data);
    TEST_PRINTLN("Need:");
    TEST_PRINTLN("%s", want->data);
    TEST_PRINTLN("Have:");
    TEST_PRINTLN("%s", res.data);

    TEST_FAILURE("");
}

static lxb_status_t
serializer_callback(const lxb_char_t *data, size_t len, void *ctx)
{
//    printf("%.*s", (int) len, (const char *) data);
    
    return LXB_STATUS_OK;
}

static lexbor_str_t *
hash_get_str(unit_kv_t *kv, unit_kv_value_t *hash, const char *name)
{
    unit_kv_value_t *data;

    data = unit_kv_hash_value_nolen_c(hash, name);
    if (data == NULL) {
        TEST_PRINTLN("Required parameter missing: %s", name);
        print_error(kv, hash);

        exit(EXIT_FAILURE);
    }

    if (unit_kv_is_string(data) == false) {
        TEST_PRINTLN("Parameter '%s' must be STRING", name);
        print_error(kv, data);

        exit(EXIT_FAILURE);
    }

    return unit_kv_string(data);
}

static fragment_entry_t
hash_get_fragment(unit_kv_t *kv, unit_kv_value_t *hash)
{
    unit_kv_value_t *data;
    lexbor_str_t *tag_name, *ns_name;
    lxb_html_tag_id_t tag_id;
    lxb_html_ns_id_t ns;
    fragment_entry_t entry = {0};

    data = unit_kv_hash_value_nolen_c(hash, "fragment");
    if (data == NULL) {
        return entry;
    }

    if (unit_kv_is_hash(data) == false) {
        TEST_PRINTLN("Parameter 'fragment' must be HASH");
        print_error(kv, data);

        exit(EXIT_FAILURE);
    }

    /* Tag and ns */
    tag_name = hash_get_str(kv, data, "tag");
    ns_name = hash_get_str(kv, data, "ns");

    tag_id = lxb_html_tag_id_by_name(NULL, tag_name->data, tag_name->length);
    if (tag_id == LXB_HTML_TAG__UNDEF) {
        TEST_PRINTLN("Unknown tag: %.*s",
                     (int) tag_name->length, tag_name->data);

        print_error(kv, data);

        exit(EXIT_FAILURE);
    }

    ns = lxb_html_ns_id_by_name(ns_name->data, ns_name->length);
    if (tag_id == LXB_HTML_TAG__UNDEF) {
        TEST_PRINTLN("Unknown namespace: %.*s",
                     (int) ns_name->length, ns_name->data);

        print_error(kv, data);

        exit(EXIT_FAILURE);
    }

    entry.tag_id = tag_id;
    entry.ns = ns;

    return entry;
}

static bool
hash_get_scripting(unit_kv_t *kv, unit_kv_value_t *hash)
{
    unit_kv_value_t *data;

    data = unit_kv_hash_value_nolen_c(hash, "scripting");
    if (data == NULL) {
        return false;
    }

    if (unit_kv_is_bool(data) == false) {
        TEST_PRINTLN("Parameter 'scripting' must be BOOL");
        print_error(kv, data);

        exit(EXIT_FAILURE);
    }

    return unit_kv_bool(data);
}

static void
print_error(unit_kv_t *kv, unit_kv_value_t *value)
{
    lexbor_str_t str;

    str = unit_kv_value_position_as_string(kv, value);
    TEST_PRINTLN("%s", str.data);
    unit_kv_string_destroy(kv, &str, false);

    str = unit_kv_value_fragment_as_string(kv, value);
    TEST_PRINTLN("%s", str.data);
    unit_kv_string_destroy(kv, &str, false);
}

int
main(int argc, const char * argv[])
{
    lxb_status_t status;

    if (argc != 2) {
        printf("Usage:\n\ttree_builder <directory path>\n");
        return EXIT_FAILURE;
    }

    TEST_INIT();
    TEST_RUN("lexbor/html/tree_builder");

    status = parse(argv[1]);
    if (status != LXB_STATUS_OK) {
        return EXIT_FAILURE;
    }

    TEST_RELEASE();
}
