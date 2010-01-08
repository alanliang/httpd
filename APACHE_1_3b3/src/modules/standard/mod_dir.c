/* ====================================================================
 * Copyright (c) 1995-1997 The Apache Group.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * 4. The names "Apache Server" and "Apache Group" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    apache@apache.org.
 *
 * 5. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * THIS SOFTWARE IS PROVIDED BY THE APACHE GROUP ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE APACHE GROUP OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Group and was originally based
 * on public domain software written at the National Center for
 * Supercomputing Applications, University of Illinois, Urbana-Champaign.
 * For more information on the Apache Group and the Apache HTTP server
 * project, please see <http://www.apache.org/>.
 *
 */

/*
 * mod_dir.c: handle default index files, and trailing-/ redirects
 */

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_request.h"
#include "http_protocol.h"
#include "http_log.h"
#include "http_main.h"
#include "util_script.h"

module MODULE_VAR_EXPORT dir_module;

typedef struct dir_config_struct {
    char *index_names;
} dir_config_rec;

#define DIR_CMD_PERMS OR_INDEXES

static command_rec dir_cmds[] =
{
    {"DirectoryIndex", set_string_slot,
     (void *) XtOffsetOf(dir_config_rec, index_names),
     DIR_CMD_PERMS, RAW_ARGS,
     "a list of file names"},
    {NULL}
};

static void *create_dir_config(pool *p, char *dummy)
{
    dir_config_rec *new =
    (dir_config_rec *) pcalloc(p, sizeof(dir_config_rec));

    new->index_names = NULL;
    return (void *) new;
}

static void *merge_dir_configs(pool *p, void *basev, void *addv)
{
    dir_config_rec *new = (dir_config_rec *) pcalloc(p, sizeof(dir_config_rec));
    dir_config_rec *base = (dir_config_rec *) basev;
    dir_config_rec *add = (dir_config_rec *) addv;

    new->index_names = add->index_names ? add->index_names : base->index_names;
    return new;
}

static int handle_dir(request_rec *r)
{
    dir_config_rec *d =
    (dir_config_rec *) get_module_config(r->per_dir_config,
                                         &dir_module);
    const char *names_ptr = d->index_names ? d->index_names : DEFAULT_INDEX;
    int error_notfound = 0;

    if (r->uri[0] == '\0' || r->uri[strlen(r->uri) - 1] != '/') {
        char *ifile;
        if (r->args != NULL)
            ifile = pstrcat(r->pool, escape_uri(r->pool, r->uri),
                            "/", "?", r->args, NULL);
        else
            ifile = pstrcat(r->pool, escape_uri(r->pool, r->uri),
                            "/", NULL);

        table_set(r->headers_out, "Location",
                  construct_url(r->pool, ifile, r->server));
        return HTTP_MOVED_PERMANENTLY;
    }

    /* KLUDGE --- make the sub_req lookups happen in the right directory.
     * Fixing this in the sub_req_lookup functions themselves is difficult,
     * and would probably break virtual includes...
     */

    if (r->filename[strlen(r->filename) - 1] != '/') {
        r->filename = pstrcat(r->pool, r->filename, "/", NULL);
    }

    while (*names_ptr) {

        char *name_ptr = getword_conf(r->pool, &names_ptr);
        request_rec *rr = sub_req_lookup_uri(name_ptr, r);

        if (rr->status == HTTP_OK && rr->finfo.st_mode != 0) {
            char *new_uri = escape_uri(r->pool, rr->uri);

            if (rr->args != NULL)
                new_uri = pstrcat(r->pool, new_uri, "?", rr->args, NULL);
            else if (r->args != NULL)
                new_uri = pstrcat(r->pool, new_uri, "?", r->args, NULL);

            destroy_sub_req(rr);
            internal_redirect(new_uri, r);
            return OK;
        }

        /* If the request returned a redirect, propagate it to the client */

        if (is_HTTP_REDIRECT(rr->status) ||
            (rr->status == HTTP_NOT_ACCEPTABLE && *names_ptr == '\0')) {

            error_notfound = rr->status;
            r->notes = overlay_tables(r->pool, r->notes, rr->notes);
            r->headers_out = overlay_tables(r->pool, r->headers_out,
                                            rr->headers_out);
            r->err_headers_out = overlay_tables(r->pool, r->err_headers_out,
                                                rr->err_headers_out);
            destroy_sub_req(rr);
            return error_notfound;
        }

        /* If the request returned something other than 404 (or 200),
         * it means the module encountered some sort of problem. To be
         * secure, we should return the error, rather than create
         * along a (possibly unsafe) directory index.
         *
         * So we store the error, and if none of the listed files
         * exist, we return the last error response we got, instead
         * of a directory listing.
         */
        if (rr->status && rr->status != HTTP_NOT_FOUND && rr->status != HTTP_OK)
            error_notfound = rr->status;

        destroy_sub_req(rr);
    }

    if (error_notfound)
        return error_notfound;

    if (r->method_number != M_GET)
        return DECLINED;

    /* nothing for us to do, pass on through */

    return DECLINED;
}


static handler_rec dir_handlers[] =
{
    {DIR_MAGIC_TYPE, handle_dir},
    {NULL}
};

module MODULE_VAR_EXPORT dir_module =
{
    STANDARD_MODULE_STUFF,
    NULL,                       /* initializer */
    create_dir_config,          /* dir config creater */
    merge_dir_configs,          /* dir merger --- default is to override */
    NULL,                       /* server config */
    NULL,                       /* merge server config */
    dir_cmds,                   /* command table */
    dir_handlers,               /* handlers */
    NULL,                       /* filename translation */
    NULL,                       /* check_user_id */
    NULL,                       /* check auth */
    NULL,                       /* check access */
    NULL,                       /* type_checker */
    NULL,                       /* fixups */
    NULL,                       /* logger */
    NULL,                       /* header parser */
    NULL,                       /* child_init */
    NULL,                       /* child_exit */
    NULL                        /* post read-request */
};