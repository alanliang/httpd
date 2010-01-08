/* ====================================================================
 * Copyright (c) 1995-1998 The Apache Group.  All rights reserved.
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
 * 5. Products derived from this software may not be called "Apache"
 *    nor may "Apache" appear in their names without prior written
 *    permission of the Apache Group.
 *
 * 6. Redistributions of any form whatsoever must retain the following
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

#define CORE_PRIVATE
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_protocol.h"	/* For index_of_response().  Grump. */
#include "http_request.h"
#include "http_conf_globals.h"
#include "http_vhost.h"
#include "http_main.h"		/* For the default_handler below... */
#include "http_log.h"
#include "rfc1413.h"
#include "util_md5.h"
#include "scoreboard.h"
#include "fnmatch.h"

#ifdef USE_MMAP_FILES
#include <sys/mman.h>

/* mmap support for static files based on ideas from John Heidemann's
 * patch against 1.0.5.  See
 * <http://www.isi.edu/~johnh/SOFTWARE/APACHE/index.html>.
 */

/* Files have to be at least this big before they're mmap()d.  This is to deal
 * with systems where the expense of doing an mmap() and an munmap() outweighs
 * the benefit for small files.  It shouldn't be set lower than 1.
 */
#ifndef MMAP_THRESHOLD
#ifdef SUNOS4
#define MMAP_THRESHOLD		(8*1024)
#else
#define MMAP_THRESHOLD		1
#endif
#endif
#endif

/* Server core module... This module provides support for really basic
 * server operations, including options and commands which control the
 * operation of other modules.  Consider this the bureaucracy module.
 *
 * The core module also defines handlers, etc., do handle just enough
 * to allow a server with the core module ONLY to actually serve documents
 * (though it slaps DefaultType on all of 'em); this was useful in testing,
 * but may not be worth preserving.
 *
 * This file could almost be mod_core.c, except for the stuff which affects
 * the http_conf_globals.
 */

static void *create_core_dir_config (pool *a, char *dir)
{
    core_dir_config *conf =
      (core_dir_config *)pcalloc(a, sizeof(core_dir_config));
  
    if (!dir || dir[strlen(dir) - 1] == '/') conf->d = dir;
    else if (strncmp(dir,"proxy:",6)==0) conf->d = pstrdup (a, dir);
    else conf->d = pstrcat (a, dir, "/", NULL);
    conf->d_is_fnmatch = conf->d ? (is_fnmatch (conf->d) != 0) : 0;
    conf->d_components = conf->d ? count_dirs (conf->d) : 0;

    conf->opts = dir ? OPT_UNSET : OPT_UNSET|OPT_ALL;
    conf->opts_add = conf->opts_remove = OPT_NONE;
    conf->override = dir ? OR_UNSET : OR_UNSET|OR_ALL;

    conf->content_md5 = 2;

    conf->use_canonical_name = 1 | 2;	/* 2 = unset, default on */

    conf->hostname_lookups = HOSTNAME_LOOKUP_UNSET;
    conf->do_rfc1413 = DEFAULT_RFC1413 | 2;  /* set bit 1 to indicate default */
    conf->satisfy = SATISFY_NOSPEC;

#ifdef RLIMIT_CPU
    conf->limit_cpu = NULL;
#endif
#if defined(RLIMIT_DATA) || defined(RLIMIT_VMEM) || defined(RLIMIT_AS)
    conf->limit_mem = NULL;
#endif
#ifdef RLIMIT_NPROC
    conf->limit_nproc = NULL;
#endif

    conf->sec = make_array (a, 2, sizeof(void *));

    return (void *)conf;
}

static void *merge_core_dir_configs (pool *a, void *basev, void *newv)
{
    core_dir_config *base = (core_dir_config *)basev;
    core_dir_config *new = (core_dir_config *)newv;
    core_dir_config *conf =
      (core_dir_config *)palloc (a, sizeof(core_dir_config));
    int i;
  
    memcpy ((char *)conf, (const char *)base, sizeof(core_dir_config));
    if( base->response_code_strings ) {
	conf->response_code_strings = palloc(a,
	    sizeof(*conf->response_code_strings) * RESPONSE_CODES );
	memcpy( conf->response_code_strings, base->response_code_strings,
	    sizeof(*conf->response_code_strings) * RESPONSE_CODES );
    }
    
    conf->d = new->d;
    conf->d_is_fnmatch = new->d_is_fnmatch;
    conf->d_components = new->d_components;
    conf->r = new->r;
    
    if (new->opts & OPT_UNSET) {
	/* there was no explicit setting of new->opts, so we merge
	 * preserve the invariant (opts_add & opts_remove) == 0
	 */
	conf->opts_add = (conf->opts_add & ~new->opts_remove) | new->opts_add;
	conf->opts_remove = (conf->opts_remove & ~new->opts_add) | new->opts_remove;
	conf->opts = (conf->opts & ~conf->opts_remove) | conf->opts_add;
        if ((base->opts & OPT_INCNOEXEC) && (new->opts & OPT_INCLUDES))
          conf->opts = (conf->opts & ~OPT_INCNOEXEC) | OPT_INCLUDES;
    }
    else {
	/* otherwise we just copy, because an explicit opts setting
	 * overrides all earlier +/- modifiers
	 */
	conf->opts = new->opts;
	conf->opts_add = new->opts_add;
	conf->opts_remove = new->opts_remove;
    }

    if (!(new->override & OR_UNSET)) conf->override = new->override;
    if (new->default_type) conf->default_type = new->default_type;
    
    if (new->auth_type) conf->auth_type = new->auth_type;
    if (new->auth_name) conf->auth_name = new->auth_name;
    if (new->requires) conf->requires = new->requires;

    if( new->response_code_strings ) {
	if( conf->response_code_strings == NULL ) {
	    conf->response_code_strings = palloc(a,
		sizeof(*conf->response_code_strings) * RESPONSE_CODES );
	    memcpy( conf->response_code_strings, new->response_code_strings,
		sizeof(*conf->response_code_strings) * RESPONSE_CODES );
	} else {
	    for (i = 0; i < RESPONSE_CODES; ++i)
		if (new->response_code_strings[i] != NULL)
		conf->response_code_strings[i] = new->response_code_strings[i];
	}
    }
    if (new->hostname_lookups != HOSTNAME_LOOKUP_UNSET)
	conf->hostname_lookups = new->hostname_lookups;
    if ((new->do_rfc1413 & 2) == 0) conf->do_rfc1413 = new->do_rfc1413;
    if ((new->content_md5 & 2) == 0) conf->content_md5 = new->content_md5;
    if ((new->use_canonical_name & 2) == 0) {
	conf->use_canonical_name = new->use_canonical_name;
    }

#ifdef RLIMIT_CPU
    if (new->limit_cpu) conf->limit_cpu = new->limit_cpu;
#endif
#if defined(RLIMIT_DATA) || defined(RLIMIT_VMEM) || defined(RLIMIT_AS)
    if (new->limit_mem) conf->limit_mem = new->limit_mem;
#endif
#ifdef RLIMIT_NPROC    
    if (new->limit_nproc) conf->limit_nproc = new->limit_nproc;
#endif

    conf->sec = append_arrays (a, base->sec, new->sec);

    if (new->satisfy != SATISFY_NOSPEC) conf->satisfy = new->satisfy;
    return (void*)conf;
}

static void *create_core_server_config (pool *a, server_rec *s)
{
    core_server_config *conf =
      (core_server_config *)pcalloc(a, sizeof(core_server_config));
    int is_virtual = s->is_virtual;
  
    conf->access_name = is_virtual ? NULL : DEFAULT_ACCESS_FNAME;
    conf->document_root = is_virtual ? NULL : DOCUMENT_LOCATION;
    conf->sec = make_array (a, 40, sizeof(void *));
    conf->sec_url = make_array (a, 40, sizeof(void *));
    
    return (void *)conf;
}

static void *merge_core_server_configs (pool *p, void *basev, void *virtv)
{
    core_server_config *base = (core_server_config *)basev;
    core_server_config *virt = (core_server_config *)virtv;
    core_server_config *conf = 
	(core_server_config *)pcalloc(p, sizeof(core_server_config));

    *conf = *virt;
    if (!conf->access_name) conf->access_name = base->access_name;
    if (!conf->document_root) conf->document_root = base->document_root;
    conf->sec = append_arrays (p, base->sec, virt->sec);
    conf->sec_url = append_arrays (p, base->sec_url, virt->sec_url);

    return conf;
}

/* Add per-directory configuration entry (for <directory> section);
 * these are part of the core server config.
 */

CORE_EXPORT(void) add_per_dir_conf (server_rec *s, void *dir_config)
{
    core_server_config *sconf = get_module_config (s->module_config,
						   &core_module);
    void **new_space = (void **) push_array (sconf->sec);
    
    *new_space = dir_config;
}

CORE_EXPORT(void) add_per_url_conf (server_rec *s, void *url_config)
{
    core_server_config *sconf = get_module_config (s->module_config,
						   &core_module);
    void **new_space = (void **) push_array (sconf->sec_url);
    
    *new_space = url_config;
}

static void add_file_conf (core_dir_config *conf, void *url_config)
{
    void **new_space = (void **) push_array (conf->sec);
    
    *new_space = url_config;
}

/* core_reorder_directories reorders the directory sections such that the
 * 1-component sections come first, then the 2-component, and so on, finally
 * followed by the "special" sections.  A section is "special" if it's a regex,
 * or if it doesn't start with / -- consider proxy: matching.  All movements
 * are in-order to preserve the ordering of the sections from the config files.
 * See directory_walk().
 */

#if defined(__EMX__) || defined(WIN32)
#define IS_SPECIAL(entry_core)	\
    ((entry_core)->r != NULL \
	|| ((entry_core)->d[0] != '/' && (entry_core)->d[1] != ':'))
#else
#define IS_SPECIAL(entry_core)	\
    ((entry_core)->r != NULL || (entry_core)->d[0] != '/')
#endif

/* We need to do a stable sort, qsort isn't stable.  So to make it stable
 * we'll be maintaining the original index into the list, and using it
 * as the minor key during sorting.  The major key is the number of
 * components (where a "special" section has infinite components).
 */
struct reorder_sort_rec {
    void *elt;
    int orig_index;
};

static int reorder_sorter (const void *va, const void *vb)
{
    const struct reorder_sort_rec *a = va;
    const struct reorder_sort_rec *b = vb;
    core_dir_config *core_a;
    core_dir_config *core_b;

    core_a = (core_dir_config *)get_module_config (a->elt, &core_module);
    core_b = (core_dir_config *)get_module_config (b->elt, &core_module);
    if (IS_SPECIAL(core_a)) {
	if (!IS_SPECIAL(core_b)) {
	    return 1;
	}
    } else if (IS_SPECIAL(core_b)) {
	return -1;
    } else {
	/* we know they're both not special */
	if (core_a->d_components < core_b->d_components) {
	    return -1;
	} else if (core_a->d_components > core_b->d_components) {
	    return 1;
	}
    }
    /* Either they're both special, or they're both not special and have the
     * same number of components.  In any event, we now have to compare
     * the minor key. */
    return a->orig_index - b->orig_index;
}

void core_reorder_directories (pool *p, server_rec *s)
{
    core_server_config *sconf;
    array_header *sec;
    struct reorder_sort_rec *sortbin;
    int nelts;
    void **elts;
    int i;

    /* XXX: we are about to waste some ram ... we will build a new array
     * and we need some scratch space to do it.  The old array and the
     * scratch space are never freed.
     */
    sconf = get_module_config (s->module_config, &core_module);
    sec = sconf->sec;
    nelts = sec->nelts;
    elts = (void **)sec->elts;

    /* build our sorting space */
    sortbin = palloc (p, sec->nelts * sizeof (*sortbin));
    for (i = 0; i < nelts; ++i) {
	sortbin[i].orig_index = i;
	sortbin[i].elt = elts[i];
    }

    qsort (sortbin, nelts, sizeof (*sortbin), reorder_sorter);

    /* and now build a new array */
    sec = make_array (p, nelts, sizeof (void *));
    for (i = 0; i < nelts; ++i) {
	*(void **)push_array (sec) = sortbin[i].elt;
    }

    sconf->sec = sec;
}

/*****************************************************************
 *
 * There are some elements of the core config structures in which
 * other modules have a legitimate interest (this is ugly, but necessary
 * to preserve NCSA back-compatibility).  So, we have a bunch of accessors
 * here...
 */

API_EXPORT(int) allow_options (request_rec *r)
{
    core_dir_config *conf = 
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module); 

    return conf->opts; 
} 

API_EXPORT(int) allow_overrides (request_rec *r) 
{ 
    core_dir_config *conf = 
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module); 

    return conf->override; 
} 

API_EXPORT(char *) auth_type (request_rec *r)
{
    core_dir_config *conf = 
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module); 

    return conf->auth_type;
}

API_EXPORT(char *) auth_name (request_rec *r)
{
    core_dir_config *conf = 
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module); 

    return conf->auth_name;
}

API_EXPORT(char *) default_type (request_rec *r)
{
    core_dir_config *conf = 
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module); 

    return conf->default_type ? conf->default_type : DEFAULT_CONTENT_TYPE;
}

API_EXPORT(char *) document_root (request_rec *r) /* Don't use this!!! */
{
    core_server_config *conf = 
      (core_server_config *)get_module_config(r->server->module_config,
					      &core_module); 

    return conf->document_root;
}

API_EXPORT(array_header *) requires (request_rec *r)
{
    core_dir_config *conf = 
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module); 

    return conf->requires;
}

API_EXPORT(int) satisfies (request_rec *r)
{
    core_dir_config *conf =
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module);

    return conf->satisfy;
}

/* Should probably just get rid of this... the only code that cares is
 * part of the core anyway (and in fact, it isn't publicised to other
 * modules).
 */

char *response_code_string (request_rec *r, int error_index)
{
    core_dir_config *conf = 
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module); 

    if( conf->response_code_strings == NULL ) {
	return NULL;
    }
    return conf->response_code_strings[error_index];
}


/* Code from Harald Hanche-Olsen <hanche@imf.unit.no> */
static ap_inline void do_double_reverse (conn_rec *conn)
{
    struct hostent *hptr;

    if (conn->double_reverse) {
	/* already done */
	return;
    }
    if (conn->remote_host == NULL || conn->remote_host[0] == '\0') {
	/* single reverse failed, so don't bother */
	conn->double_reverse = -1;
	return;
    }
    hptr = gethostbyname(conn->remote_host);
    if (hptr) {
	char **haddr;

	for (haddr = hptr->h_addr_list; *haddr; haddr++) {
	    if (((struct in_addr *)(*haddr))->s_addr
		== conn->remote_addr.sin_addr.s_addr) {
		conn->double_reverse = 1;
		return;
	    }
	}
    }
    conn->double_reverse = -1;
}

API_EXPORT(const char *) get_remote_host(conn_rec *conn, void *dir_config, int type)
{
    struct in_addr *iaddr;
    struct hostent *hptr;
    int hostname_lookups;
#ifdef STATUS
    int old_stat = SERVER_DEAD;	/* we shouldn't ever be in this state */
#endif

    /* If we haven't checked the host name, and we want to */
    if (dir_config) {
	hostname_lookups =
	    ((core_dir_config *)get_module_config(dir_config, &core_module))
		->hostname_lookups;
	if (hostname_lookups == HOSTNAME_LOOKUP_UNSET) {
	    hostname_lookups = HOSTNAME_LOOKUP_OFF;
	}
    } else {
	/* the default */
	hostname_lookups = HOSTNAME_LOOKUP_OFF;
    }

    if (type != REMOTE_NOLOOKUP
	&& conn->remote_host == NULL
	&& (type == REMOTE_DOUBLE_REV
	    || hostname_lookups != HOSTNAME_LOOKUP_OFF)) {
#ifdef STATUS
	old_stat = update_child_status(conn->child_num, SERVER_BUSY_DNS,
					    (request_rec*)NULL);
#endif /* STATUS */
	iaddr = &(conn->remote_addr.sin_addr);
	hptr = gethostbyaddr((char *)iaddr, sizeof(struct in_addr), AF_INET);
	if (hptr != NULL) {
	    conn->remote_host = pstrdup(conn->pool, (void *)hptr->h_name);
	    str_tolower (conn->remote_host);
	   
	    if (hostname_lookups == HOSTNAME_LOOKUP_DOUBLE) {
		do_double_reverse (conn);
		if (conn->double_reverse != 1) {
		    conn->remote_host = NULL;
		}
	    }
	}
	/* if failed, set it to the NULL string to indicate error */
	if (conn->remote_host == NULL) conn->remote_host = "";
    }
    if (type == REMOTE_DOUBLE_REV) {
	do_double_reverse (conn);
	if (conn->double_reverse == -1) {
	    return NULL;
	}
    }
#ifdef STATUS
    if (old_stat != SERVER_DEAD) {
	(void)update_child_status(conn->child_num,old_stat,(request_rec*)NULL);
    }
#endif /* STATUS */

/*
 * Return the desired information; either the remote DNS name, if found,
 * or either NULL (if the hostname was requested) or the IP address
 * (if any identifier was requested).
 */
    if (conn->remote_host != NULL && conn->remote_host[0] != '\0')
	return conn->remote_host;
    else
    {
	if (type == REMOTE_HOST || type == REMOTE_DOUBLE_REV) return NULL;
	else return conn->remote_ip;
    }
}

API_EXPORT(const char *) get_remote_logname(request_rec *r)
{
    core_dir_config *dir_conf;

    if (r->connection->remote_logname != NULL)
	return r->connection->remote_logname;

/* If we haven't checked the identity, and we want to */
    dir_conf = (core_dir_config *)
	get_module_config(r->per_dir_config, &core_module);

    if (dir_conf->do_rfc1413 & 1)
	return rfc1413(r->connection, r->server);
    else
	return NULL;
}

/* There are two options regarding what the "name" of a server is.  The
 * "canonical" name as defined by ServerName and Port, or the "client's
 * name" as supplied by a possible Host: header or full URI.  We never
 * trust the port passed in the client's headers, we always use the
 * port of the actual socket.
 */
API_EXPORT(const char *) get_server_name(const request_rec *r)
{
    core_dir_config *d =
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module);
    
    if (d->use_canonical_name & 1) {
	return r->server->server_hostname;
    }
    return r->hostname ? r->hostname : r->server->server_hostname;
}

API_EXPORT(unsigned) get_server_port(const request_rec *r)
{
    unsigned port;
    core_dir_config *d =
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module);
    
    port = r->server->port ? r->server->port : default_port(r);

    if (d->use_canonical_name & 1) {
	return port;
    }
    return r->hostname ? ntohs(r->connection->local_addr.sin_port)
			: port;
}

API_EXPORT(char *) construct_url(pool *p, const char *uri, const request_rec *r)
{
    unsigned port;
    const char *host;
    core_dir_config *d =
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module);

    if (d->use_canonical_name & 1) {
	port = r->server->port ? r->server->port : default_port(r);
	host = r->server->server_hostname;
    }
    else {
        if (r->hostname)
            port = ntohs(r->connection->local_addr.sin_port);
        else if (r->server->port)
            port = r->server->port;
        else
            port = default_port(r);

	host = r->hostname ? r->hostname : r->server->server_hostname;
    }
    if (is_default_port(port, r)) {
	return pstrcat(p, http_method(r), "://", host, uri, NULL);
    }
    return psprintf(p, "%s://%s:%u%s", http_method(r), host, port, uri);
}

/*****************************************************************
 *
 * Commands... this module handles almost all of the NCSA httpd.conf
 * commands, but most of the old srm.conf is in the the modules.
 */

static const char end_directory_section[] = "</Directory>";
static const char end_directorymatch_section[] = "</DirectoryMatch>";
static const char end_location_section[] = "</Location>";
static const char end_locationmatch_section[] = "</LocationMatch>";
static const char end_files_section[] = "</Files>";
static const char end_filesmatch_section[] = "</FilesMatch>";
static const char end_virtualhost_section[] = "</VirtualHost>";
static const char end_ifmodule_section[] = "</IfModule>";


API_EXPORT(const char *) check_cmd_context(cmd_parms *cmd, unsigned forbidden)
{
    const char *gt = (cmd->cmd->name[0] == '<'
		   && cmd->cmd->name[strlen(cmd->cmd->name)-1] != '>') ? ">" : "";

    if ((forbidden & NOT_IN_VIRTUALHOST) && cmd->server->is_virtual)
	return pstrcat(cmd->pool, cmd->cmd->name, gt,
		       " cannot occur within <VirtualHost> section", NULL);

    if ((forbidden & NOT_IN_LIMIT) && cmd->limited != -1)
	return pstrcat(cmd->pool, cmd->cmd->name, gt,
		       " cannot occur within <Limit> section", NULL);

    if ((forbidden & NOT_IN_DIR_LOC_FILE) == NOT_IN_DIR_LOC_FILE && cmd->path != NULL)
	return pstrcat(cmd->pool, cmd->cmd->name, gt,
		       " cannot occur within <Directory/Location/Files> section", NULL);
    
    if (((forbidden & NOT_IN_DIRECTORY) && (cmd->end_token == end_directory_section
	    || cmd->end_token == end_directorymatch_section)) ||
	((forbidden & NOT_IN_LOCATION) && (cmd->end_token == end_location_section
	    || cmd->end_token == end_locationmatch_section)) ||
	((forbidden & NOT_IN_FILES) && (cmd->end_token == end_files_section
	    || cmd->end_token == end_filesmatch_section)))
	
	return pstrcat(cmd->pool, cmd->cmd->name, gt,
		       " cannot occur within <", cmd->end_token+2,
		       " section", NULL);

    return NULL;
}

static const char *set_access_name (cmd_parms *cmd, void *dummy, char *arg)
{
    void *sconf = cmd->server->module_config;
    core_server_config *conf = get_module_config (sconf, &core_module);

    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    conf->access_name = pstrdup(cmd->pool, arg);
    return NULL;
}

static const char *set_document_root (cmd_parms *cmd, void *dummy, char *arg)
{
    void *sconf = cmd->server->module_config;
    core_server_config *conf = get_module_config (sconf, &core_module);
  
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    if (!is_directory (arg)) {
	if (cmd->server->is_virtual) {
	    fprintf (stderr, "Warning: DocumentRoot [%s] does not exist\n", arg);
	}
	else {
	    return "DocumentRoot must be a directory";
	}
    }
    
    conf->document_root = arg;
    return NULL;
}

static const char *set_error_document (cmd_parms *cmd, core_dir_config *conf,
				char *line)
{
    int error_number, index_number, idx500;
    char *w;
                
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);
    if (err != NULL) return err;

    /* 1st parameter should be a 3 digit number, which we recognize;
     * convert it into an array index
     */
  
    w = getword_conf_nc (cmd->pool, &line);
    error_number = atoi(w);

    idx500 = index_of_response(HTTP_INTERNAL_SERVER_ERROR);

    if (error_number == HTTP_INTERNAL_SERVER_ERROR)
        index_number = idx500;
    else if ((index_number = index_of_response(error_number)) == idx500)
        return pstrcat(cmd->pool, "Unsupported HTTP response code ", w, NULL);
                
    /* Store it... */

    if( conf->response_code_strings == NULL ) {
	conf->response_code_strings = pcalloc(cmd->pool,
	    sizeof(*conf->response_code_strings) * RESPONSE_CODES );
    }
    conf->response_code_strings[index_number] = pstrdup (cmd->pool, line);

    return NULL;
}

/* access.conf commands...
 *
 * The *only* thing that can appear in access.conf at top level is a
 * <Directory> section.  NB we need to have a way to cut the srm_command_loop
 * invoked by dirsection (i.e., <Directory>) short when </Directory> is seen.
 * We do that by returning an error, which dirsection itself recognizes and
 * discards as harmless.  Cheesy, but it works.
 */

static const char *set_override (cmd_parms *cmd, core_dir_config *d, const char *l)
{
    char *w;
  
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);
    if (err != NULL) {
        return err;
    }

    d->override = OR_NONE;
    while(l[0]) {
        w = getword_conf (cmd->pool, &l);
	if(!strcasecmp(w,"Limit"))
	    d->override |= OR_LIMIT;
	else if(!strcasecmp(w,"Options"))
	    d->override |= OR_OPTIONS;
	else if(!strcasecmp(w,"FileInfo"))
            d->override |= OR_FILEINFO;
	else if(!strcasecmp(w,"AuthConfig"))
	    d->override |= OR_AUTHCFG;
	else if(!strcasecmp(w,"Indexes"))
            d->override |= OR_INDEXES;
	else if(!strcasecmp(w,"None"))
	    d->override = OR_NONE;
	else if(!strcasecmp(w,"All")) 
	    d->override = OR_ALL;
	else 
	    return pstrcat (cmd->pool, "Illegal override option ", w, NULL);
	d->override &= ~OR_UNSET;
    }

    return NULL;
}

static const char *set_options (cmd_parms *cmd, core_dir_config *d, const char *l)
{
    allow_options_t opt;
    int first = 1;
    char action;

    while(l[0]) {
        char *w = getword_conf(cmd->pool, &l);
	action = '\0';

	if (*w == '+' || *w == '-')
	    action = *(w++);
	else if (first) {
  	    d->opts = OPT_NONE;
            first = 0;
        }
	    
	if(!strcasecmp(w,"Indexes"))
	    opt = OPT_INDEXES;
	else if(!strcasecmp(w,"Includes"))
	    opt = OPT_INCLUDES;
	else if(!strcasecmp(w,"IncludesNOEXEC"))
	    opt = (OPT_INCLUDES | OPT_INCNOEXEC);
	else if(!strcasecmp(w,"FollowSymLinks"))
	    opt = OPT_SYM_LINKS;
	else if(!strcasecmp(w,"SymLinksIfOwnerMatch"))
	    opt = OPT_SYM_OWNER;
	else if(!strcasecmp(w,"execCGI"))
	    opt = OPT_EXECCGI;
	else if (!strcasecmp(w,"MultiViews"))
	    opt = OPT_MULTI;
	else if (!strcasecmp(w,"RunScripts")) /* AI backcompat. Yuck */
	    opt = OPT_MULTI|OPT_EXECCGI;
	else if(!strcasecmp(w,"None")) 
	    opt = OPT_NONE;
	else if(!strcasecmp(w,"All")) 
	    opt = OPT_ALL;
	else 
	    return pstrcat (cmd->pool, "Illegal option ", w, NULL);

	/* we ensure the invariant (d->opts_add & d->opts_remove) == 0 */
	if (action == '-') {
	    d->opts_remove |= opt;
	    d->opts_add &= ~opt;
	    d->opts &= ~opt;
	}
	else if (action == '+') {
	    d->opts_add |= opt;
	    d->opts_remove &= ~opt;
	    d->opts |= opt;
	}
	else {
	    d->opts |= opt;
	}
    }

    return NULL;
}

static const char *satisfy (cmd_parms *cmd, core_dir_config *c, char *arg)
{
    if(!strcasecmp(arg,"all"))
        c->satisfy = SATISFY_ALL;
    else if(!strcasecmp(arg,"any"))
        c->satisfy = SATISFY_ANY;
    else
        return "Satisfy either 'any' or 'all'.";
    return NULL;
}

static const char *require (cmd_parms *cmd, core_dir_config *c, char *arg)
{
    require_line *r;
  
    if (!c->requires)
        c->requires = make_array (cmd->pool, 2, sizeof(require_line));
    
    r = (require_line *)push_array (c->requires);
    r->requirement = pstrdup (cmd->pool, arg);
    r->method_mask = cmd->limited;
    return NULL;
}

CORE_EXPORT_NONSTD(const char *) limit_section (cmd_parms *cmd, void *dummy, const char *arg)
{
    const char *limited_methods = getword(cmd->pool,&arg,'>');
    int limited = 0;
  
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);
    if (err != NULL) return err;

    /* XXX: NB: Currently, we have no way of checking
     * whether <Limit> sections are closed properly.
     * (If we would add a srm_command_loop() here we might...)
     */
    
    while(limited_methods[0]) {
        char *method = getword_conf (cmd->pool, &limited_methods);
	if(!strcasecmp(method,"GET")) limited |= (1 << M_GET);
	else if(!strcasecmp(method,"PUT")) limited |= (1 << M_PUT);
	else if(!strcasecmp(method,"POST")) limited |= (1 << M_POST);
	else if(!strcasecmp(method,"DELETE")) limited |= (1 << M_DELETE);
        else if(!strcasecmp(method,"CONNECT")) limited |= (1 << M_CONNECT);
	else if(!strcasecmp(method,"OPTIONS")) limited |= (1 << M_OPTIONS);
	else return "unknown method in <Limit>";
    }

    cmd->limited = limited;
    return NULL;
}

static const char *endlimit_section (cmd_parms *cmd, void *dummy, void *dummy2)
{
    if (cmd->limited == -1) return "</Limit> unexpected";
    
    cmd->limited = -1;
    return NULL;
}

/*
 * When a section is not closed properly when end-of-file is reached,
 * then an error message should be printed:
 */
static const char *missing_endsection (cmd_parms *cmd, int nest)
{
    if (nest < 2)
	return psprintf(cmd->pool, "Missing %s directive at end-of-file",
		    cmd->end_token);
    return psprintf(cmd->pool, "%d missing %s directives at end-of-file",
		    nest, cmd->end_token);
}

/* We use this in <DirectoryMatch> and <FilesMatch>, to ensure that 
 * people don't get bitten by wrong-cased regex matches
 */

#ifdef WIN32
#define USE_ICASE REG_ICASE
#else
#define USE_ICASE 0
#endif

static const char *end_nested_section(cmd_parms *cmd, void *dummy)
{
    if (cmd->end_token == NULL) {
	return pstrcat(cmd->pool, cmd->cmd->name,
	    " without matching <", cmd->cmd->name + 2, " section", NULL);
    }
    if (cmd->cmd->name != cmd->end_token) {
	return pstrcat(cmd->pool, "Expected ", cmd->end_token, " but saw ",
	    cmd->cmd->name, NULL);
    }
    return cmd->end_token;
}

static const char *dirsection (cmd_parms *cmd, void *dummy, const char *arg)
{
    const char *errmsg;
    char *endp = strrchr (arg, '>');
    int old_overrides = cmd->override;
    char *old_path = cmd->path;
    core_dir_config *conf;
    void *new_dir_conf = create_per_dir_config (cmd->pool);
    regex_t *r = NULL;
    const char *old_end_token;
    const command_rec *thiscmd = cmd->cmd;

    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    if (endp) *endp = '\0';

    cmd->path = getword_conf (cmd->pool, &arg);
#ifdef __EMX__
    /* Fix OS/2 HPFS filename case problem. */
    cmd->path = strlwr(cmd->path);
#endif    
    cmd->override = OR_ALL|ACCESS_CONF;

    if (thiscmd->cmd_data) { /* <DirectoryMatch> */
	r = pregcomp(cmd->pool, cmd->path, REG_EXTENDED|USE_ICASE);
    }
    else if (!strcmp(cmd->path, "~")) {
	cmd->path = getword_conf (cmd->pool, &arg);
	r = pregcomp(cmd->pool, cmd->path, REG_EXTENDED|USE_ICASE);
    }
    else {
	/* Ensure that the pathname is canonical */
	cmd->path = os_canonical_filename(cmd->pool, cmd->path);
    }

    old_end_token = cmd->end_token;
    cmd->end_token = thiscmd->cmd_data ? end_directorymatch_section : end_directory_section;
    errmsg = srm_command_loop (cmd, new_dir_conf);
    if (errmsg == NULL) {
	errmsg = missing_endsection(cmd, 1);
    }
    cmd->end_token = old_end_token;
    if (errmsg != (thiscmd->cmd_data ? end_directorymatch_section : end_directory_section))
	return errmsg;

    conf = (core_dir_config *)get_module_config(new_dir_conf, &core_module);
    conf->r = r;

    add_per_dir_conf (cmd->server, new_dir_conf);

    if (*arg != '\0')
	return pstrcat (cmd->pool, "Multiple ", thiscmd->name,
			"> arguments not (yet) supported.", NULL);

    cmd->path = old_path;
    cmd->override = old_overrides;

    return NULL;
}

static const char *urlsection (cmd_parms *cmd, void *dummy, const char *arg)
{
    const char *errmsg;
    char *endp = strrchr (arg, '>');
    int old_overrides = cmd->override;
    char *old_path = cmd->path;
    core_dir_config *conf;
    regex_t *r = NULL;
    const char *old_end_token;
    const command_rec *thiscmd = cmd->cmd;

    void *new_url_conf = create_per_dir_config (cmd->pool);

    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    if (endp) *endp = '\0';

    cmd->path = getword_conf (cmd->pool, &arg);
    cmd->override = OR_ALL|ACCESS_CONF;

    if (thiscmd->cmd_data) { /* <LocationMatch> */
	r = pregcomp(cmd->pool, cmd->path, REG_EXTENDED);
    }
    else if (!strcmp(cmd->path, "~")) {
	cmd->path = getword_conf (cmd->pool, &arg);
	r = pregcomp(cmd->pool, cmd->path, REG_EXTENDED);
    }

    old_end_token = cmd->end_token;
    cmd->end_token = thiscmd->cmd_data ? end_locationmatch_section : end_location_section;
    errmsg = srm_command_loop (cmd, new_url_conf);
    if (errmsg == NULL) {
	errmsg = missing_endsection(cmd, 1);
    }
    cmd->end_token = old_end_token;
    if (errmsg != (thiscmd->cmd_data ? end_locationmatch_section : end_location_section))
	return errmsg;

    conf = (core_dir_config *)get_module_config(new_url_conf, &core_module);
    conf->d = pstrdup(cmd->pool, cmd->path);	/* No mangling, please */
    conf->d_is_fnmatch = is_fnmatch( conf->d ) != 0;
    conf->r = r;

    add_per_url_conf (cmd->server, new_url_conf);
    
    if (*arg != '\0')
	return pstrcat (cmd->pool, "Multiple ", thiscmd->name,
			"> arguments not (yet) supported.", NULL);

    cmd->path = old_path;
    cmd->override = old_overrides;

    return NULL;
}

static const char *filesection (cmd_parms *cmd, core_dir_config *c, const char *arg)
{
    const char *errmsg;
    char *endp = strrchr (arg, '>');
    int old_overrides = cmd->override;
    char *old_path = cmd->path;
    core_dir_config *conf;
    regex_t *r = NULL;
    const char *old_end_token;
    const command_rec *thiscmd = cmd->cmd;

    void *new_file_conf = create_per_dir_config (cmd->pool);

    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT | NOT_IN_LOCATION);
    if (err != NULL) return err;

    if (endp) *endp = '\0';

    cmd->path = getword_conf (cmd->pool, &arg);
    /* Only if not an .htaccess file */
    if (!old_path)
	cmd->override = OR_ALL|ACCESS_CONF;

    if (thiscmd->cmd_data) { /* <FilesMatch> */
        r = pregcomp(cmd->pool, cmd->path, REG_EXTENDED|USE_ICASE);
    }
    else if (!strcmp(cmd->path, "~")) {
	cmd->path = getword_conf (cmd->pool, &arg);
	r = pregcomp(cmd->pool, cmd->path, REG_EXTENDED|USE_ICASE);
    }
    else {
	/* Ensure that the pathname is canonical */
	cmd->path = os_canonical_filename(cmd->pool, cmd->path);
    }

    old_end_token = cmd->end_token;
    cmd->end_token = thiscmd->cmd_data ? end_filesmatch_section : end_files_section;
    errmsg = srm_command_loop (cmd, new_file_conf);
    if (errmsg == NULL) {
	errmsg = missing_endsection(cmd, 1);
    }
    cmd->end_token = old_end_token;
    if (errmsg != (thiscmd->cmd_data ? end_filesmatch_section : end_files_section))
	return errmsg;

    conf = (core_dir_config *)get_module_config(new_file_conf, &core_module);
    conf->d = cmd->path;
    conf->d_is_fnmatch = is_fnmatch(conf->d) != 0;
    conf->r = r;

    add_file_conf (c, new_file_conf);

    if (*arg != '\0')
	return pstrcat (cmd->pool, "Multiple ", thiscmd->name,
			"> arguments not (yet) supported.", NULL);

    cmd->path = old_path;
    cmd->override = old_overrides;

    return NULL;
}

/* XXX: NB: Currently, we have no way of checking
 * whether <IfModule> sections are closed properly.
 * Extra (redundant, unpaired) </IfModule> directives are
 * simply silently ignored.
 */
static const char *end_ifmod (cmd_parms *cmd, void *dummy) {
    return NULL;
}

static const char *start_ifmod (cmd_parms *cmd, void *dummy, char *arg)
{
    char *endp = strrchr (arg, '>');
    char l[MAX_STRING_LEN];
    int not = (arg[0] == '!');
    module *found;
    int nest = 1;

    if (endp) *endp = '\0';
    if (not) arg++;

    found = find_linked_module(arg);

    if ((!not && found) || (not && !found))
      return NULL;

    while (nest && !(cfg_getline (l, MAX_STRING_LEN, cmd->config_file))) {
        if (!strncasecmp(l, "<IfModule", 9))
	  nest++;
	if (!strcasecmp(l, "</IfModule>"))
	  nest--;
    }

    if (nest) {
	cmd->end_token = end_ifmodule_section;
	return missing_endsection(cmd, nest);
    }
    return NULL;
}

/* httpd.conf commands... beginning with the <VirtualHost> business */

static const char *virtualhost_section (cmd_parms *cmd, void *dummy, char *arg)
{
    server_rec *main_server = cmd->server, *s;
    const char *errmsg;
    char *endp = strrchr (arg, '>');
    pool *p = cmd->pool, *ptemp = cmd->temp_pool;
    const char *old_end_token;

    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    if (endp) *endp = '\0';
    
    /* FIXME: There's another feature waiting to happen here -- since you
	can now put multiple addresses/names on a single <VirtualHost>
	you might want to use it to group common definitions and then
	define other "subhosts" with their individual differences.  But
	personally I'd rather just do it with a macro preprocessor. -djg */
    if (main_server->is_virtual)
	return "<VirtualHost> doesn't nest!";
    
    errmsg = init_virtual_host (p, arg, main_server, &s);
    if (errmsg)
	return errmsg;

    s->next = main_server->next;
    main_server->next = s;
	
    old_end_token = cmd->end_token;
    cmd->end_token = end_virtualhost_section;
    cmd->server = s;
    errmsg = srm_command_loop (cmd, s->lookup_defaults);
    cmd->server = main_server;
    if (errmsg == NULL) {
	errmsg = missing_endsection(cmd, 1);
    }
    cmd->end_token = old_end_token;

    if (s->srm_confname)
	process_resource_config (s, s->srm_confname, p, ptemp);

    if (s->access_confname)
	process_resource_config (s, s->access_confname, p, ptemp);
    
    if (errmsg == end_virtualhost_section)
	return NULL;
    return errmsg;
}

static const char *set_server_alias(cmd_parms *cmd, void *dummy, const char *arg)
{
    if (!cmd->server->names)
	return "ServerAlias only used in <VirtualHost>";
    while (*arg) {
	char **item, *name = getword_conf(cmd->pool, &arg);
	if (is_matchexp(name))
	    item = (char **) push_array(cmd->server->wild_names);
	else
	    item = (char **) push_array(cmd->server->names);
	*item = name;
    }
    return NULL;
}

static const char *add_module_command (cmd_parms *cmd, void *dummy, char *arg)
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    if (add_named_module (arg))
        return NULL;
    return "required module not found";
}

static const char *clear_module_list_command (cmd_parms *cmd, void *dummy)
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    clear_module_list ();
    return NULL;
}

static const char *set_server_string_slot (cmd_parms *cmd, void *dummy,
		                                   char *arg)
{
    /* This one's pretty generic... */
  
    int offset = (int)(long)cmd->info;
    char *struct_ptr = (char *)cmd->server;
    
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    *(char **)(struct_ptr + offset) = arg;
    return NULL;
}

static const char *server_type (cmd_parms *cmd, void *dummy, char *arg)
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    if (!strcasecmp (arg, "inetd")) standalone = 0;
    else if (!strcasecmp (arg, "standalone")) standalone = 1;
    else return "ServerType must be either 'inetd' or 'standalone'";

    return NULL;
}

static const char *server_port (cmd_parms *cmd, void *dummy, char *arg)
{
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    int port;

    if (err != NULL) 
	return err;
    port = atoi(arg);
    if (port <= 0 || port >= 65536) /* 65536 == 1<<16 */
	return pstrcat(cmd->temp_pool, "The port number \"", arg, 
		       "\" is outside the appropriate range (i.e. 1..65535).",
		       NULL);
    cmd->server->port = port;
    return NULL;
}

static const char *set_signature_flag (cmd_parms *cmd, core_dir_config *d, 
		                               char *arg)
{
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);
    if (err != NULL) return err;

    if (strcasecmp(arg, "On") == 0)
	d->server_signature = srv_sig_on;
    else if (strcasecmp(arg, "Off") == 0)
	d->server_signature = srv_sig_off;
    else if (strcasecmp(arg, "EMail") == 0)
	d->server_signature = srv_sig_withmail;
    else
	return "ServerSignature: use one of: off | on | email";
    return NULL;
}

static const char *set_send_buffer_size (cmd_parms *cmd, void *dummy, char *arg)
{
    int s = atoi (arg);
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    if (s < 512 && s != 0) {
        return "SendBufferSize must be >= 512 bytes, or 0 for system default.";
    }
    cmd->server->send_buffer_size = s;
    return NULL;
}

static const char *set_user (cmd_parms *cmd, void *dummy, char *arg)
{
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    if (!cmd->server->is_virtual) {
	user_name = arg;
	cmd->server->server_uid = user_id = uname2id(arg);
    }
    else {
	if (suexec_enabled)
	    cmd->server->server_uid = uname2id(arg);
	else {
	    cmd->server->server_uid = user_id;
	    fprintf(stderr,
		"Warning: User directive in <VirtualHost> "
		"requires SUEXEC wrapper.\n");
	}
    }
#if !defined (BIG_SECURITY_HOLE) && !defined (__EMX__)
    if (cmd->server->server_uid == 0) {
	fprintf (stderr,
"Error:\tApache has not been designed to serve pages while running\n"
"\tas root.  There are known race conditions that will allow any\n"
"\tlocal user to read any file on the system.  Should you still\n"
"\tdesire to serve pages as root then add -DBIG_SECURITY_HOLE to\n"
"\tthe EXTRA_CFLAGS line in your src/Configuration file and rebuild\n"
"\tthe server.  It is strongly suggested that you instead modify the\n"
"\tUser directive in your httpd.conf file to list a non-root user.\n");
	exit (1);
    }
#endif

    return NULL;
}

static const char *set_group (cmd_parms *cmd, void *dummy, char *arg)
{
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    if (!cmd->server->is_virtual)
	cmd->server->server_gid = group_id = gname2id(arg);
    else {
	if (suexec_enabled)
	    cmd->server->server_gid = gname2id(arg);
	else {
	    cmd->server->server_gid = group_id;
	    fprintf(stderr,
		    "Warning: Group directive in <VirtualHost> requires SUEXEC wrapper.\n");
	}
    }

    return NULL;
}

static const char *set_server_root (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    if (!is_directory (arg)) return "ServerRoot must be a valid directory";
    ap_cpystrn (server_root, arg, sizeof(server_root));
    return NULL;
}

static const char *set_timeout (cmd_parms *cmd, void *dummy, char *arg) {
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    cmd->server->timeout = atoi (arg);
    return NULL;
}

static const char *set_keep_alive_timeout (cmd_parms *cmd, void *dummy,
		                                   char *arg)
{
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    cmd->server->keep_alive_timeout = atoi (arg);
    return NULL;
}

static const char *set_keep_alive (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    /* We've changed it to On/Off, but used to use numbers
     * so we accept anything but "Off" or "0" as "On"
     */
    if (!strcasecmp(arg, "off") || !strcmp(arg, "0"))
	cmd->server->keep_alive = 0;
    else
	cmd->server->keep_alive = 1;
    return NULL;
}

static const char *set_keep_alive_max (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    cmd->server->keep_alive_max = atoi (arg);
    return NULL;
}

static const char *set_pidfile (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    if (cmd->server->is_virtual)
	return "PidFile directive not allowed in <VirtualHost>";
    pid_fname = arg;
    return NULL;
}

static const char *set_scoreboard (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    scoreboard_fname = arg;
    return NULL;
}

static const char *set_lockfile (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    lock_fname = arg;
    return NULL;
}

static const char *set_idcheck (cmd_parms *cmd, core_dir_config *d, int arg) 
{
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);
    if (err != NULL) return err;

    d->do_rfc1413 = arg != 0;
    return NULL;
}

static const char *set_hostname_lookups (cmd_parms *cmd, core_dir_config *d,
		                                 char *arg)
{
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);
    if (err != NULL) return err;

    if (!strcasecmp (arg, "on")) {
	d->hostname_lookups = HOSTNAME_LOOKUP_ON;
    } else if (!strcasecmp (arg, "off")) {
	d->hostname_lookups = HOSTNAME_LOOKUP_OFF;
    } else if (!strcasecmp (arg, "double")) {
	d->hostname_lookups = HOSTNAME_LOOKUP_DOUBLE;
    } else {
	return "parameter must be 'on', 'off', or 'double'";
    }
    return NULL;
}

static const char *set_serverpath (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

    cmd->server->path = arg;
    cmd->server->pathlen = strlen (arg);
    return NULL;
}

static const char *set_content_md5 (cmd_parms *cmd, core_dir_config *d, int arg) 
{
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);
    if (err != NULL) return err;

    d->content_md5 = arg != 0;
    return NULL;
}

static const char *set_use_canonical_name (cmd_parms *cmd, core_dir_config *d, 
		                                   int arg)
{
    const char *err = check_cmd_context(cmd, NOT_IN_LIMIT);

    if (err != NULL)
	return err;
    
    d->use_canonical_name = arg != 0;
    return NULL;
}

static const char *set_daemons_to_start (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    daemons_to_start = atoi (arg);
    return NULL;
}

static const char *set_min_free_servers (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    daemons_min_free = atoi (arg);
    if (daemons_min_free <= 0) {
       fprintf(stderr, "WARNING: detected MinSpareServers set to non-positive.\n");
       fprintf(stderr, "Resetting to 1 to avoid almost certain Apache failure.\n");
       fprintf(stderr, "Please read the documentation.\n");
       daemons_min_free = 1;
    }
       
    return NULL;
}

static const char *set_max_free_servers (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    daemons_max_free = atoi (arg);
    return NULL;
}

static const char *set_server_limit (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    daemons_limit = atoi (arg);
    if (daemons_limit > HARD_SERVER_LIMIT) {
       fprintf(stderr, "WARNING: MaxClients of %d exceeds compile time limit "
           "of %d servers,\n", daemons_limit, HARD_SERVER_LIMIT);
       fprintf(stderr, " lowering MaxClients to %d.  To increase, please "
           "see the\n", HARD_SERVER_LIMIT);
       fprintf(stderr, " HARD_SERVER_LIMIT define in src/httpd.h.\n");
       daemons_limit = HARD_SERVER_LIMIT;
    } else if (daemons_limit < 1) {
	fprintf (stderr, "WARNING: Require MaxClients > 0, setting to 1\n");
	daemons_limit = 1;
    }
    return NULL;
}

static const char *set_max_requests (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    max_requests_per_child = atoi (arg);
    return NULL;
}

static const char *set_threads (cmd_parms *cmd, void *dummy, char *arg) {
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    threads_per_child = atoi (arg);
    return NULL;
}

static const char *set_excess_requests (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    excess_requests_per_child = atoi (arg);
    return NULL;
}


#if defined(RLIMIT_CPU) || defined(RLIMIT_DATA) || defined(RLIMIT_VMEM) || defined(RLIMIT_NPROC) || defined(RLIMIT_AS)
static void set_rlimit(cmd_parms *cmd, struct rlimit **plimit, const char *arg,
                       const char * arg2, int type)
{
    char *str;
    struct rlimit *limit;
    /* If your platform doesn't define rlim_t then typedef it in conf.h */
    rlim_t cur = 0;
    rlim_t max = 0;

    *plimit = (struct rlimit *)pcalloc(cmd->pool,sizeof **plimit);
    limit = *plimit;
    if ((getrlimit(type, limit)) != 0)	{
	*plimit = NULL;
	aplog_error(APLOG_MARK, APLOG_ERR, cmd->server,
		    "%s: getrlimit failed", cmd->cmd->name);
	return;
    }

    if ((str = getword_conf(cmd->pool, &arg))) {
	if (!strcasecmp(str, "max")) {
	    cur = limit->rlim_max;
	}
	else {
	    cur = atol(str);
	}
    }
    else {
	aplog_error(APLOG_MARK, APLOG_NOERRNO|APLOG_ERR, cmd->server,
		    "Invalid parameters for %s", cmd->cmd->name);
	return;
    }
    
    if (arg2 && (str = getword_conf(cmd->pool, &arg2)))
	max = atol(str);

    /* if we aren't running as root, cannot increase max */
    if (geteuid()) {
	limit->rlim_cur = cur;
	if (max)
	    aplog_error(APLOG_MARK, APLOG_NOERRNO|APLOG_ERR, cmd->server,
			"Must be uid 0 to raise maximum %s", cmd->cmd->name);
    }
    else {
	if (cur)
	    limit->rlim_cur = cur;
	if (max)
	    limit->rlim_max = max;
    }
}
#endif

#if !defined (RLIMIT_CPU) || !(defined (RLIMIT_DATA) || defined (RLIMIT_VMEM) || defined(RLIMIT_AS)) || !defined (RLIMIT_NPROC)
static const char *no_set_limit (cmd_parms *cmd, core_dir_config *conf,
				 char *arg, char *arg2)
{
    aplog_error(APLOG_MARK, APLOG_NOERRNO|APLOG_ERR, cmd->server,
		"%s not supported on this platform", cmd->cmd->name);
    return NULL;
}
#endif

#ifdef RLIMIT_CPU
static const char *set_limit_cpu (cmd_parms *cmd, core_dir_config *conf, 
	                          char *arg, char *arg2)
{
    set_rlimit(cmd,&conf->limit_cpu,arg,arg2,RLIMIT_CPU);
    return NULL;
}
#endif

#if defined (RLIMIT_DATA) || defined (RLIMIT_VMEM) || defined(RLIMIT_AS)
static const char *set_limit_mem (cmd_parms *cmd, core_dir_config *conf, 
	                          char *arg, char * arg2)
{
#if defined(RLIMIT_AS)
    set_rlimit(cmd,&conf->limit_mem,arg,arg2,RLIMIT_AS);
#elif defined(RLIMIT_DATA)
    set_rlimit(cmd,&conf->limit_mem,arg,arg2,RLIMIT_DATA);
#elif defined(RLIMIT_VMEM)
    set_rlimit(cmd,&conf->limit_mem,arg,arg2,RLIMIT_VMEM);
#endif
    return NULL;
}
#endif

#ifdef RLIMIT_NPROC
static const char *set_limit_nproc (cmd_parms *cmd, core_dir_config *conf,  
	                            char *arg, char * arg2)
{
    set_rlimit(cmd,&conf->limit_nproc,arg,arg2,RLIMIT_NPROC);
    return NULL;
}
#endif

static const char *set_bind_address (cmd_parms *cmd, void *dummy, char *arg) 
{
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    bind_address.s_addr = get_virthost_addr (arg, NULL);
    return NULL;
}

static const char *set_listener(cmd_parms *cmd, void *dummy, char *ips)
{
    listen_rec *new;
    char *ports;
    unsigned short port;

    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    ports=strchr(ips, ':');
    if (ports != NULL)
    {
	if (ports == ips) return "Missing IP address";
	else if (ports[1] == '\0')
	    return "Address must end in :<port-number>";
	*(ports++) = '\0';
    } else
	ports = ips;

    new=pcalloc(cmd->pool, sizeof(listen_rec));
    new->local_addr.sin_family = AF_INET;
    if (ports == ips) /* no address */
	new->local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else
	new->local_addr.sin_addr.s_addr = get_virthost_addr(ips, NULL);
    port=atoi(ports);
    if(!port)
	return "Port must be numeric";
    new->local_addr.sin_port = htons(port);
    new->fd = -1;
    new->used = 0;
    new->next = listeners;
    listeners = new;
    return NULL;
}

static const char *set_listenbacklog (cmd_parms *cmd, void *dummy, char *arg) 
{
    int b;

    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    b = atoi (arg);
    if (b < 1) return "ListenBacklog must be > 0";
    listenbacklog = b;
    return NULL;
}

static const char *set_coredumpdir (cmd_parms *cmd, void *dummy, char *arg) 
{
    struct stat finfo;
    const char *err = check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) return err;

    if ((stat(arg, &finfo) == -1) || !S_ISDIR(finfo.st_mode)) {
	return pstrcat(cmd->pool, "CoreDumpDirectory ", arg, 
	    " does not exist or is not a directory", NULL);
    }
    ap_cpystrn(coredump_dir, arg, sizeof(coredump_dir));
    return NULL;
}

static const char *include_config (cmd_parms *cmd, void *dummy, char *name)
{
    name = server_root_relative(cmd->pool, name);
    
    process_resource_config(cmd->server, name, cmd->pool, cmd->temp_pool);

    return NULL;
}

static const char *set_loglevel (cmd_parms *cmd, void *dummy, const char *arg) 
{
   char *str;
    
    const char *err = check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE|NOT_IN_LIMIT);
    if (err != NULL) return err;

   if ((str = getword_conf(cmd->pool, &arg))) {
       if (!strcasecmp(str, "emerg"))
	   cmd->server->loglevel = APLOG_EMERG;
       else if (!strcasecmp(str, "alert"))
	   cmd->server->loglevel = APLOG_ALERT;
       else if (!strcasecmp(str, "crit"))
	   cmd->server->loglevel = APLOG_CRIT;
       else if (!strcasecmp(str, "error"))
	   cmd->server->loglevel = APLOG_ERR;
       else if (!strcasecmp(str, "warn"))
	   cmd->server->loglevel = APLOG_WARNING;
       else if (!strcasecmp(str, "notice"))
	   cmd->server->loglevel = APLOG_NOTICE;
       else if (!strcasecmp(str, "info"))
	   cmd->server->loglevel = APLOG_INFO;
       else if (!strcasecmp(str, "debug"))
	   cmd->server->loglevel = APLOG_DEBUG;
       else
           return "LogLevel requires level keyword: one of emerg/alert/crit/error/warn/notice/info/debug";
   }
   else
       return "LogLevel requires level keyword";
   
   return NULL;
}

API_EXPORT(const char *) psignature(const char *prefix, request_rec *r)
{
    char sport[20];
    core_dir_config *conf =
    (core_dir_config *) get_module_config(r->per_dir_config, &core_module);

    if (conf->server_signature == srv_sig_off)
	return "";

    ap_snprintf(sport, sizeof sport, "%u", (unsigned) r->server->port);

    if (conf->server_signature == srv_sig_withmail) {
	return pstrcat(r->pool, prefix, "<ADDRESS>" SERVER_BASEVERSION
	     " Server at <A HREF=\"mailto:", r->server->server_admin, "\">",
		       r->server->server_hostname, "</A> Port ", sport,
		       "</ADDRESS>\n", NULL);
    }
    return pstrcat(r->pool, prefix, "<ADDRESS>" SERVER_BASEVERSION
	     " Server at ", r->server->server_hostname, "</A> Port ", sport,
		   "</ADDRESS>\n", NULL);
}

/*
 * Load an authorisation realm into our location configuration, applying the
 * usual rules that apply to realms.
 */
static const char *set_authname(cmd_parms *cmd, void *mconfig, char *word1)
{
    core_dir_config *aconfig = (core_dir_config *)mconfig;

    aconfig->auth_name = ap_escape_quotes(cmd->pool, word1);
    return NULL;
}

/* Note --- ErrorDocument will now work from .htaccess files.  
 * The AllowOverride of Fileinfo allows webmasters to turn it off
 */

static const command_rec core_cmds[] = {

/* Old access config file commands */

{ "<Directory", dirsection, NULL, RSRC_CONF, RAW_ARGS, "Container for directives affecting resources located in the specified directories" },
{ end_directory_section, end_nested_section, NULL, ACCESS_CONF, NO_ARGS, "Marks end of <Directory>" },
{ "<Location", urlsection, NULL, RSRC_CONF, RAW_ARGS, "Container for directives affecting resources accessed through the specified URL paths" },
{ end_location_section, end_nested_section, NULL, ACCESS_CONF, NO_ARGS, "Marks end of <Location>" },
{ "<VirtualHost", virtualhost_section, NULL, RSRC_CONF, RAW_ARGS, "Container to map directives to a particular virtual host, takes one or more host addresses" },
{ end_virtualhost_section, end_nested_section, NULL, RSRC_CONF, NO_ARGS, "Marks end of <VirtualHost>" },
{ "<Files", filesection, NULL, OR_ALL, RAW_ARGS, "Container for directives affecting files matching specified patterns" },
{ end_files_section, end_nested_section, NULL, OR_ALL, NO_ARGS, "Marks end of <Files>" },
{ "<Limit", limit_section, NULL, OR_ALL, RAW_ARGS, "Container for authentication directives when accessed using specified HTTP methods" },
{ "</Limit>", endlimit_section, NULL, OR_ALL, NO_ARGS, "Marks end of <Limit>" },
{ "<IfModule", start_ifmod, NULL, OR_ALL, RAW_ARGS, "Container for directives based on existance of specified modules" },
{ end_ifmodule_section, end_ifmod, NULL, OR_ALL, NO_ARGS, "Marks end of <IfModule>" },
{ "<DirectoryMatch", dirsection, (void*)1, RSRC_CONF, RAW_ARGS, "Container for directives affecting resources located in the specified directories" },
{ end_directorymatch_section, end_nested_section, NULL, ACCESS_CONF, NO_ARGS, "Marks end of <DirectoryMatch>" },
{ "<LocationMatch", urlsection, (void*)1, RSRC_CONF, RAW_ARGS, "Container for directives affecting resources accessed through the specified URL paths" },
{ end_locationmatch_section, end_nested_section, NULL, ACCESS_CONF, NO_ARGS, "Marks end of <LocationMatch>" },
{ "<FilesMatch", filesection, (void*)1, OR_ALL, RAW_ARGS, "Container for directives affecting files matching specified patterns" },
{ end_filesmatch_section, end_nested_section, NULL, OR_ALL, NO_ARGS, "Marks end of <FilesMatch>" },
{ "AuthType", set_string_slot, (void*)XtOffsetOf(core_dir_config, auth_type),
    OR_AUTHCFG, TAKE1, "An HTTP authorization type (e.g., \"Basic\")" },
{ "AuthName", set_authname, NULL, OR_AUTHCFG, TAKE1,
    "The authentication realm (e.g. \"Members Only\")" },
{ "Require", require, NULL, OR_AUTHCFG, RAW_ARGS, "Selects which authenticated users or groups may access a protected space" },
{ "Satisfy", satisfy, NULL, OR_AUTHCFG, TAKE1,
    "access policy if both allow and require used ('all' or 'any')" },    

/* Old resource config file commands */
  
{ "AccessFileName", set_access_name, NULL, RSRC_CONF, RAW_ARGS, "Name(s) of per-directory config files (default: .htaccess)" },
{ "DocumentRoot", set_document_root, NULL, RSRC_CONF, TAKE1, "Root directory of the document tree"  },
{ "ErrorDocument", set_error_document, NULL, OR_FILEINFO, RAW_ARGS, "Change responses for HTTP errors" },
{ "AllowOverride", set_override, NULL, ACCESS_CONF, RAW_ARGS, "Controls what groups of directives can be configured by per-directory config files" },
{ "Options", set_options, NULL, OR_OPTIONS, RAW_ARGS, "Set a number of attributes for a given directory" },
{ "DefaultType", set_string_slot,
    (void*)XtOffsetOf (core_dir_config, default_type),
    OR_FILEINFO, TAKE1, "the default MIME type for untypable files" },

/* Old server config file commands */

{ "ServerType", server_type, NULL, RSRC_CONF, TAKE1,"'inetd' or 'standalone'"},
{ "Port", server_port, NULL, RSRC_CONF, TAKE1, "A TCP port number"},
{ "HostnameLookups", set_hostname_lookups, NULL, ACCESS_CONF|RSRC_CONF, TAKE1, "\"on\" to enable, \"off\" to disable reverse DNS lookups, or \"double\" to enable double-reverse DNS lookups" },
{ "User", set_user, NULL, RSRC_CONF, TAKE1, "Effective user id for this server"},
{ "Group", set_group, NULL, RSRC_CONF, TAKE1, "Effective group id for this server"},
{ "ServerAdmin", set_server_string_slot,
  (void *)XtOffsetOf (server_rec, server_admin), RSRC_CONF, TAKE1,
  "The email address of the server administrator" },
{ "ServerName", set_server_string_slot,
  (void *)XtOffsetOf (server_rec, server_hostname), RSRC_CONF, TAKE1,
  "The hostname of the server" },
{ "ServerSignature", set_signature_flag, NULL, ACCESS_CONF|RSRC_CONF, TAKE1,
  "En-/disable server signature (on|off|email)" },
{ "ServerRoot", set_server_root, NULL, RSRC_CONF, TAKE1, "Common directory of server-related files (logs, confs, etc)" },
{ "ErrorLog", set_server_string_slot,
  (void *)XtOffsetOf (server_rec, error_fname), RSRC_CONF, TAKE1,
  "The filename of the error log" },
{ "PidFile", set_pidfile, NULL, RSRC_CONF, TAKE1,
    "A file for logging the server process ID"},
{ "ScoreBoardFile", set_scoreboard, NULL, RSRC_CONF, TAKE1,
    "A file for Apache to maintain runtime process management information"},
{ "LockFile", set_lockfile, NULL, RSRC_CONF, TAKE1,
    "The lockfile used when Apache needs to lock the accept() call"},
{ "AccessConfig", set_server_string_slot,
  (void *)XtOffsetOf (server_rec, access_confname), RSRC_CONF, TAKE1,
  "The filename of the access config file" },
{ "ResourceConfig", set_server_string_slot,
  (void *)XtOffsetOf (server_rec, srm_confname), RSRC_CONF, TAKE1,
  "The filename of the resource config file" },
{ "ServerAlias", set_server_alias, NULL, RSRC_CONF, RAW_ARGS,
  "A name or names alternately used to access the server" },
{ "ServerPath", set_serverpath, NULL, RSRC_CONF, TAKE1,
  "The pathname the server can be reached at" },
{ "Timeout", set_timeout, NULL, RSRC_CONF, TAKE1, "Timeout duration (sec)"},
{ "KeepAliveTimeout", set_keep_alive_timeout, NULL, RSRC_CONF, TAKE1, "Keep-Alive timeout duration (sec)"},
{ "MaxKeepAliveRequests", set_keep_alive_max, NULL, RSRC_CONF, TAKE1, "Maximum number of Keep-Alive requests per connection, or 0 for infinite" },
{ "KeepAlive", set_keep_alive, NULL, RSRC_CONF, TAKE1, "Whether persistent connections should be On or Off" },
{ "IdentityCheck", set_idcheck, NULL, RSRC_CONF|ACCESS_CONF, FLAG, "Enable identd (RFC 1413) user lookups - SLOW" },
{ "ContentDigest", set_content_md5, NULL, RSRC_CONF|ACCESS_CONF|OR_AUTHCFG, FLAG, "whether or not to send a Content-MD5 header with each request" },
{ "UseCanonicalName", set_use_canonical_name, NULL, RSRC_CONF|ACCESS_CONF|OR_AUTHCFG, FLAG, "whether or not to always use the canonical ServerName : Port when constructing URLs" },
{ "StartServers", set_daemons_to_start, NULL, RSRC_CONF, TAKE1, "Number of child processes launched at server startup" },
{ "MinSpareServers", set_min_free_servers, NULL, RSRC_CONF, TAKE1, "Minimum number of idle children, to handle request spikes" },
{ "MaxSpareServers", set_max_free_servers, NULL, RSRC_CONF, TAKE1, "Maximum number of idle children" },
{ "MaxServers", set_max_free_servers, NULL, RSRC_CONF, TAKE1, "Deprecated equivalent to MaxSpareServers" },
{ "ServersSafetyLimit", set_server_limit, NULL, RSRC_CONF, TAKE1, "Deprecated equivalent to MaxClients" },
{ "MaxClients", set_server_limit, NULL, RSRC_CONF, TAKE1, "Maximum number of children alive at the same time" },
{ "MaxRequestsPerChild", set_max_requests, NULL, RSRC_CONF, TAKE1, "Maximum number of requests a particular child serves before dying." },
{ "RLimitCPU",
#ifdef RLIMIT_CPU
 set_limit_cpu, (void*)XtOffsetOf(core_dir_config, limit_cpu),
#else
 no_set_limit, NULL,
#endif
      OR_ALL, TAKE12, "soft/hard limits for max CPU usage in seconds" },
{ "RLimitMEM",
#if defined (RLIMIT_DATA) || defined (RLIMIT_VMEM) || defined (RLIMIT_AS)
 set_limit_mem, (void*)XtOffsetOf(core_dir_config, limit_mem),
#else
 no_set_limit, NULL,
#endif
      OR_ALL, TAKE12, "soft/hard limits for max memory usage per process" },
{ "RLimitNPROC",
#ifdef RLIMIT_NPROC
 set_limit_nproc, (void*)XtOffsetOf(core_dir_config, limit_nproc),
#else
 no_set_limit, NULL,
#endif
      OR_ALL, TAKE12, "soft/hard limits for max number of processes per uid" },
{ "BindAddress", set_bind_address, NULL, RSRC_CONF, TAKE1,
  "'*', a numeric IP address, or the name of a host with a unique IP address"},
{ "Listen", set_listener, NULL, RSRC_CONF, TAKE1,
      "a port number or a numeric IP address and a port number"},
{ "SendBufferSize", set_send_buffer_size, NULL, RSRC_CONF, TAKE1, "send buffer size in bytes"},
{ "AddModule", add_module_command, NULL, RSRC_CONF, ITERATE,
  "the name of a module" },
{ "ClearModuleList", clear_module_list_command, NULL, RSRC_CONF, NO_ARGS, NULL },
{ "ThreadsPerChild", set_threads, NULL, RSRC_CONF, TAKE1, "Number of threads a child creates" },
{ "ExcessRequestsPerChild", set_excess_requests, NULL, RSRC_CONF, TAKE1, "Maximum number of requests a particular child serves after it is ready to die." },
{ "ListenBacklog", set_listenbacklog, NULL, RSRC_CONF, TAKE1, "maximum length of the queue of pending connections, as used by listen(2)" },
{ "CoreDumpDirectory", set_coredumpdir, NULL, RSRC_CONF, TAKE1, "The location of the directory Apache changes to before dumping core" },
{ "Include", include_config, NULL, RSRC_CONF, TAKE1, "config file to be included" },
{ "LogLevel", set_loglevel, NULL, RSRC_CONF, TAKE1, "set level of verbosity in error logging" },
{ "NameVirtualHost", set_name_virtual_host, NULL, RSRC_CONF, TAKE1,
  "a numeric ip address:port, or the name of a host" },
{ NULL },
};

/*****************************************************************
 *
 * Core handlers for various phases of server operation...
 */

static int core_translate (request_rec *r)
{
    void *sconf = r->server->module_config;
    core_server_config *conf = get_module_config (sconf, &core_module);
  
    if (r->proxyreq) return HTTP_FORBIDDEN;
    if ((r->uri[0] != '/') && strcmp(r->uri, "*")) {
	aplog_error(APLOG_MARK, APLOG_NOERRNO|APLOG_ERR, r->server,
		    "Invalid URI in request %s", r->the_request);
	return BAD_REQUEST;
    }
    
    if (r->server->path &&
	!strncmp(r->uri, r->server->path, r->server->pathlen) &&
	(r->server->path[r->server->pathlen - 1] == '/' ||
	 r->uri[r->server->pathlen] == '/' ||
	 r->uri[r->server->pathlen] == '\0'))
      r->filename = pstrcat (r->pool, conf->document_root,
			     (r->uri + r->server->pathlen), NULL);
    else
      r->filename = pstrcat (r->pool, conf->document_root, r->uri, NULL);

    return OK;
}

static int do_nothing (request_rec *r) { return OK; }

#ifdef USE_MMAP_FILES
struct mmap {
    void *mm;
    size_t length;
};

static void mmap_cleanup (void *mmv)
{
    struct mmap *mmd = mmv;

    munmap(mmd->mm, mmd->length);
}
#endif

/*
 * Default handler for MIME types without other handlers.  Only GET
 * and OPTIONS at this point... anyone who wants to write a generic
 * handler for PUT or POST is free to do so, but it seems unwise to provide
 * any defaults yet... So, for now, we assume that this will always be
 * the last handler called and return 405 or 501.
 */

static int default_handler (request_rec *r)
{
    core_dir_config *d =
      (core_dir_config *)get_module_config(r->per_dir_config, &core_module);
    int rangestatus, errstatus;
    FILE *f;
#ifdef USE_MMAP_FILES
    caddr_t mm;
#endif

    /* This handler has no use for a request body (yet), but we still
     * need to read and discard it if the client sent one.
     */
    if ((errstatus = discard_request_body(r)) != OK)
        return errstatus;

    r->allowed |= (1 << M_GET) | (1 << M_OPTIONS);

    if (r->method_number == M_INVALID) {
	aplog_error(APLOG_MARK, APLOG_NOERRNO|APLOG_ERR, r->server,
		    "Invalid method in request %s", r->the_request);
	return NOT_IMPLEMENTED;
    }
    if (r->method_number == M_OPTIONS) return send_http_options(r);
    if (r->method_number == M_PUT) return METHOD_NOT_ALLOWED;

    if (r->finfo.st_mode == 0 || (r->path_info && *r->path_info)) {
	aplog_error(APLOG_MARK, APLOG_ERR|APLOG_NOERRNO, r->server, 
                    "File does not exist: %s", r->path_info ? 
                    pstrcat(r->pool, r->filename, r->path_info, NULL)
		    : r->filename);
	return NOT_FOUND;
    }
    if (r->method_number != M_GET) return METHOD_NOT_ALLOWED;
	
#if defined(__EMX__) || defined(WIN32)
    /* Need binary mode for OS/2 */
    f = pfopen (r->pool, r->filename, "rb");
#else
    f = pfopen (r->pool, r->filename, "r");
#endif

    if (f == NULL) {
        aplog_error(APLOG_MARK, APLOG_ERR, r->server,
		    "file permissions deny server access: %s", r->filename);
        return FORBIDDEN;
    }
	
    update_mtime (r, r->finfo.st_mtime);
    set_last_modified(r);
    set_etag(r);
    if (((errstatus = meets_conditions(r)) != OK)
	|| (errstatus = set_content_length (r, r->finfo.st_size))) {
	    return errstatus;
    }

#ifdef USE_MMAP_FILES
    block_alarms();
    if ((r->finfo.st_size >= MMAP_THRESHOLD)
	&& ( !r->header_only || (d->content_md5 & 1))) {
	/* we need to protect ourselves in case we die while we've got the
 	 * file mmapped */
	mm = mmap (NULL, r->finfo.st_size, PROT_READ, MAP_PRIVATE,
		    fileno(f), 0);
	if (mm == (caddr_t)-1) {
	    aplog_error(APLOG_MARK, APLOG_CRIT, r->server,
			"default_handler: mmap failed: %s", r->filename);
	}
    } else {
	mm = (caddr_t)-1;
    }

    if (mm == (caddr_t)-1) {
	unblock_alarms();
#endif

	if (d->content_md5 & 1) {
	    table_setn(r->headers_out, "Content-MD5", ap_md5digest(r->pool, f));
	}

	rangestatus = set_byterange(r);
#ifdef CHARSET_EBCDIC
	/* To make serving of "raw ASCII text" files easy (they serve faster 
	 * since they don't have to be converted from EBCDIC), a new
	 * "magic" type prefix was invented: text/x-ascii-{plain,html,...}
	 * If we detect one of these content types here, we simply correct
	 * the type to the real text/{plain,html,...} type. Otherwise, we
	 * set a flag that translation is required later on.
	 */
        os_checkconv(r);
#endif /*CHARSET_EBCDIC*/

	send_http_header (r);
	
	if (!r->header_only) {
	    if (!rangestatus)
		send_fd (f, r);
	    else {
		long offset, length;
		while (each_byterange(r, &offset, &length)) {
		    fseek(f, offset, SEEK_SET);
		    send_fd_length(f, r, length);
		}
	    }
	}

#ifdef USE_MMAP_FILES
    } else {
	struct mmap *mmd;

	mmd = palloc (r->pool, sizeof (*mmd));
	mmd->mm = mm;
	mmd->length = r->finfo.st_size;
	register_cleanup (r->pool, (void *)mmd, mmap_cleanup, mmap_cleanup);
	unblock_alarms();

	if (d->content_md5 & 1) {
	    AP_MD5_CTX context;
	    
	    MD5Init(&context);
	    MD5Update(&context, (void *)mm, r->finfo.st_size);
	    table_setn(r->headers_out, "Content-MD5",
		ap_md5contextTo64(r->pool, &context));
	}

	rangestatus = set_byterange(r);
	send_http_header (r);
	
	if (!r->header_only) {
	    if (!rangestatus)
		send_mmap (mm, r, 0, r->finfo.st_size);
	    else {
		long offset, length;
		while (each_byterange(r, &offset, &length)) {
		    send_mmap(mm, r, offset, length);
		}
	    }
	}
    }
#endif

    pfclose(r->pool, f);
    return OK;
}

static const handler_rec core_handlers[] = {
{ "*/*", default_handler },
{ NULL }
};

API_VAR_EXPORT module core_module = {
   STANDARD_MODULE_STUFF,
   NULL,			/* initializer */
   create_core_dir_config,	/* create per-directory config structure */
   merge_core_dir_configs,	/* merge per-directory config structures */
   create_core_server_config,	/* create per-server config structure */
   merge_core_server_configs,	/* merge per-server config structures */
   core_cmds,			/* command table */
   core_handlers,		/* handlers */
   core_translate,		/* translate_handler */
   NULL,			/* check_user_id */
   NULL,			/* check auth */
   do_nothing,			/* check access */
   do_nothing,			/* type_checker */
   NULL,			/* pre-run fixups */
   NULL,			/* logger */
   NULL,			/* header parser */
   NULL,			/* child_init */
   NULL,			/* child_exit */
   NULL				/* post_read_request */
};