/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe pipeline dumping for debug purposes
 */

#include <upipe/ubase.h>
#include <upipe/upipe.h>
#include <upipe/upipe_dump.h>
#include <upipe/uprobe_prefix.h>

#include <stdio.h>
#include <errno.h>
#include <assert.h>

/** @This is a structure allocated per pipe. */
struct upipe_dump_ctx {
    /** unique ID for pipe input */
    uint64_t input_uid;
    /** unique ID for pipe output */
    uint64_t output_uid;
    /** true if the output graph was already dumped */
    bool output_dumped;
    /** original uchain */
    struct uchain original_uchain;
    /** original opaque */
    void *original_opaque;
};

/** @hidden */
static void upipe_dump_pipe(upipe_dump_pipe_label pipe_label,
                            upipe_dump_flow_def_label flow_def_label,
                            FILE *file, struct upipe *upipe, uint64_t *uid_p,
                            struct uchain *list, bool no_output);

/** @This converts a pipe to a label (default function).
 *
 * @param upipe upipe structure
 * @return allocated string
 */
char *upipe_dump_upipe_label_default(struct upipe *upipe)
{
    struct uprobe *uprobe = upipe->uprobe;
    const char *prefix = NULL;

    while (uprobe != NULL && prefix == NULL) {
        prefix = uprobe_pfx_get_name(uprobe);
        uprobe = uprobe->next;
    }

    char *string = malloc(strlen(prefix) + sizeof(" (aaaa)"));
    sprintf(string, "%s (%4.4s)", prefix ?: "",
            (const char *)&upipe->mgr->signature);
    return string;
}

/** @This converts a flow def to a label (default function).
 *
 * @param flow_def flow definition packet
 * @return allocated string
 */
char *upipe_dump_flow_def_label_default(struct uref *flow_def)
{
    if (flow_def == NULL)
        return strdup("");

    const char *def = NULL;
    uref_flow_get_def(flow_def, &def);
    if (def == NULL)
        return strdup("");

    char *string = malloc(strlen(def) * 2);
    string[0] = '\0';
    const char *p, *q = def;
    while ((p = strchr(q, '.')) != NULL) {
        strncat(string, q, p - q);
        strcat(string, "\\l");
        q = p + 1;
    }
    return string;
}

/** @internal @This finds in the list of a pipe has already been printed.
 *
 * @param upipe first pipe of the pipeline
 * @param list list of already printed pipes
 * @return true if the pipe has been found
 */
static bool upipe_dump_find(struct upipe *upipe, struct uchain *list)
{
    struct uchain *uchain;
    ulist_foreach (list, uchain) {
        if (uchain == upipe_to_uchain(upipe))
            return true;
    }
    return false;
}

/** @internal @This dumps the outputs of a pipe and its subpipes.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param file file pointer to write to
 * @param upipe first pipe of the pipeline
 * @param uid_p pointer to unique ID
 * @param list list of already printed pipes
 */
static void upipe_dump_output(upipe_dump_pipe_label pipe_label,
                              upipe_dump_flow_def_label flow_def_label,
                              FILE *file, struct upipe *upipe,
                              uint64_t *uid_p, struct uchain *list)
{
    struct upipe_dump_ctx *ctx =
        upipe_get_opaque(upipe, struct upipe_dump_ctx *);
    ctx->output_dumped = true;

    /* Iterate over subpipes. */
    struct upipe *sub = NULL;
    while (ubase_check(upipe_iterate_sub(upipe, &sub)) && sub != NULL) {
        struct upipe_dump_ctx *sub_ctx =
                upipe_get_opaque(sub, struct upipe_dump_ctx *);
        if (!sub_ctx->output_dumped)
            upipe_dump_output(pipe_label, flow_def_label, file, sub,
                              uid_p, list);
    }

    /* Edge with output. */
    struct upipe *output = NULL;
    upipe_get_output(upipe, &output);
    if (output == NULL)
        return;

    upipe_dump_pipe(pipe_label, flow_def_label, file, output,
                    uid_p, list, false);

    struct uref *flow_def = NULL;
    upipe_get_flow_def(upipe, &flow_def);
    char *label = flow_def_label(flow_def);

    struct upipe_dump_ctx *output_ctx =
            upipe_get_opaque(output, struct upipe_dump_ctx *);
    fprintf(file, "pipe%"PRIu64"->pipe%"PRIu64" [label=\"%s\"];\n",
            ctx->output_uid, output_ctx->input_uid, label);
    free(label);
}

/** @internal @This dumps the inner pipes of a pipe.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param file file pointer to write to
 * @param first_inner first inner pipe
 * @param last_inner last inner pipe
 * @param uid_p pointer to unique ID
 * @param list list of already printed pipes
 */
static void upipe_dump_inner(upipe_dump_pipe_label pipe_label,
                             upipe_dump_flow_def_label flow_def_label,
                             FILE *file, struct upipe *first_inner,
                             struct upipe *last_inner,
                             uint64_t *uid_p, struct uchain *list)
{
    upipe_dump_pipe(pipe_label, flow_def_label, file, first_inner,
                    uid_p, list, true);
    if (first_inner == last_inner)
        return;

    struct upipe_dump_ctx *first_ctx =
        upipe_get_opaque(first_inner, struct upipe_dump_ctx *);

    /* Edge with output. */
    struct upipe *output = NULL;
    upipe_get_output(first_inner, &output);
    if (output == NULL)
        return;

    upipe_dump_inner(pipe_label, flow_def_label, file, output, last_inner,
                     uid_p, list);

    struct uref *flow_def = NULL;
    upipe_get_flow_def(first_inner, &flow_def);
    char *label = flow_def_label(flow_def);

    struct upipe_dump_ctx *output_ctx =
            upipe_get_opaque(output, struct upipe_dump_ctx *);
    fprintf(file, "pipe%"PRIu64"->pipe%"PRIu64" [label=\"%s\"];\n",
            first_ctx->output_uid, output_ctx->input_uid, label);
    free(label);
}

/** @internal @This dumps a pipe in dot format.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param file file pointer to write to
 * @param upipe upipe to dump
 * @param uid_p pointer to unique ID
 * @param list list of already printed pipes
 * @param no_output true if the output will be dumped from another context
 */
static void upipe_dump_pipe(upipe_dump_pipe_label pipe_label,
                            upipe_dump_flow_def_label flow_def_label,
                            FILE *file, struct upipe *upipe, uint64_t *uid_p,
                            struct uchain *list, bool no_output)
{
    if (upipe_dump_find(upipe, list))
        return;

    char *label = pipe_label(upipe);

    /* Prepare context. */
    struct upipe_dump_ctx *ctx = malloc(sizeof(struct upipe_dump_ctx));
    assert(ctx != NULL);
    ctx->input_uid = (*uid_p)++;
    ctx->output_dumped = !no_output;
    ctx->original_uchain = upipe->uchain;
    ctx->original_opaque = upipe->opaque;
    upipe_set_opaque(upipe, ctx);
    ulist_add(list, upipe_to_uchain(upipe));

    /* Dig into inner pipes. */
    upipe_bin_freeze(upipe);
    struct upipe *first_inner = NULL;
    struct upipe *last_inner = NULL;
    upipe_bin_get_first_inner(upipe, &first_inner);
    upipe_bin_get_last_inner(upipe, &last_inner);
    if (first_inner != NULL || last_inner != NULL) {
        first_inner = first_inner ?: last_inner;
        last_inner = last_inner ?: first_inner;
        ctx->output_uid = (*uid_p)++;

        fprintf(file, "subgraph cluster_%"PRIu64" {\n", ctx->input_uid);
        fprintf(file, "color=\"#0e0e0e\";\n");
        fprintf(file, "fillcolor=\"#e0e0e0\";\n");
        fprintf(file, "style=\"dashed,filled\";\n");
        fprintf(file, "label=\"%s\";\n", label);

        fprintf(file, "pipe%"PRIu64" [label=\"input\", style=\"dashed,filled\"];\n",
                ctx->input_uid);
        fprintf(file, "pipe%"PRIu64" [label=\"output\", style=\"dashed,filled\"];\n",
                ctx->output_uid);

        upipe_dump_inner(pipe_label, flow_def_label, file,
                         first_inner, last_inner, uid_p, list);
        upipe_dump_inner(pipe_label, flow_def_label, file,
                         last_inner, last_inner, uid_p, list);

        struct upipe_dump_ctx *first_ctx =
                upipe_get_opaque(first_inner, struct upipe_dump_ctx *);
        struct upipe_dump_ctx *last_ctx =
                upipe_get_opaque(last_inner, struct upipe_dump_ctx *);
        fprintf(file, "pipe%"PRIu64"->pipe%"PRIu64";\n",
                ctx->input_uid, first_ctx->input_uid);
        fprintf(file, "pipe%"PRIu64"->pipe%"PRIu64";\n",
                last_ctx->output_uid, ctx->output_uid);
        fprintf(file, "}\n");

    } else {
        ctx->output_uid = ctx->input_uid;
        fprintf(file, "pipe%"PRIu64" [label=\"%s\"];\n", ctx->input_uid, label);
    }
    upipe_bin_thaw(upipe);

    /* Iterate over subpipes. */
    struct upipe *sub = NULL;
    while (ubase_check(upipe_iterate_sub(upipe, &sub)) && sub != NULL) {
        upipe_dump_pipe(pipe_label, flow_def_label, file, sub,
                        uid_p, list, true);

        struct upipe_dump_ctx *sub_ctx =
            upipe_get_opaque(sub, struct upipe_dump_ctx *);
        fprintf(file, "pipe%"PRIu64"->pipe%"PRIu64" [style=\"dashed\"];\n",
                ctx->input_uid, sub_ctx->input_uid);
        fprintf(file, "{rank=same; pipe%"PRIu64" pipe%"PRIu64"};\n",
                ctx->input_uid, sub_ctx->input_uid);
    }

    if (!no_output)
        upipe_dump_output(pipe_label, flow_def_label, file, upipe, uid_p, list);

    free(label);
}

/** @This dumps a pipeline in dot format.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param file file pointer to write to
 * @param args list of sources pipes terminated with NULL
 */
void upipe_dump_va(upipe_dump_pipe_label pipe_label,
                   upipe_dump_flow_def_label flow_def_label,
                   FILE *file, va_list args)
{
    pipe_label = pipe_label ?: upipe_dump_upipe_label_default;
    flow_def_label = flow_def_label ?: upipe_dump_flow_def_label_default;

    uint64_t uid = 0;
    struct uchain list;
    struct uchain *uchain, *uchain_tmp;
    ulist_init(&list);

    fprintf(file, "digraph \"upipe dump\" {\n");
    fprintf(file, "graph [bgcolor=\"#00000000\", fontname=\"Arial\", fontsize=10, fontcolor=\"#0e0e0e\"];\n");
    fprintf(file, "edge [penwidth=1, color=\"#0e0e0e\", fontname=\"Arial\", fontsize=7, fontcolor=\"#0e0e0e\"];\n");
    fprintf(file, "node [shape=\"box\", style=\"filled\", color=\"#0e0e0e\", fillcolor=\"#f6f6f6\", fontname=\"Arial\", fontsize=10, fontcolor=\"#0e0e0e\"];\n");
    fprintf(file, "newrank=true;\n"); /* for rank=same */

    struct upipe *source;
    while ((source = va_arg(args, struct upipe *)) != NULL)
        upipe_dump_pipe(pipe_label, flow_def_label, file, source,
                        &uid, &list, false);

    /* Walk through the super-pipes that we may have forgotten. */
    ulist_foreach (&list, uchain) {
        struct upipe *upipe = upipe_from_uchain(uchain);
        struct upipe *super;
        if (ubase_check(upipe_sub_get_super(upipe, &super)) && super != NULL)
            upipe_dump_pipe(pipe_label, flow_def_label, file, super,
                            &uid, &list, false);
    }

    fprintf(file, "}\n");

    /* Clean up. */
    ulist_delete_foreach (&list, uchain, uchain_tmp) {
        struct upipe *upipe = upipe_from_uchain(uchain);
        struct upipe_dump_ctx *ctx =
            upipe_get_opaque(upipe, struct upipe_dump_ctx *);
        upipe->uchain = ctx->original_uchain;
        upipe->opaque = ctx->original_opaque;
        uid--;
        if (ctx->output_uid != ctx->input_uid)
            uid--;
        free(ctx);
    }
    assert(!uid);
}


/** @This opens a file and dumps a pipeline in dot format.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param path path of the file to open
 * @param args list of sources pipes terminated with NULL
 * @return an error code
 */
int upipe_dump_open_va(upipe_dump_pipe_label pipe_label,
                       upipe_dump_flow_def_label flow_def_label,
                       const char *path, va_list args)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
        return UBASE_ERR_EXTERNAL;

    upipe_dump_va(pipe_label, flow_def_label, file, args);
    fclose(file);
    return UBASE_ERR_NONE;
}
