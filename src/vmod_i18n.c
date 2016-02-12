/* TODO Sort by priority */
/* TODO filter languages for only the ones we support */
/* TODO allow supported_languages without ':' chars */
/* TODO free the pl list in vmod_free */



/**
 * This code is offered under the Open Source BSD license.
 * 
 * Copyright (c) 2016 Opera Software. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 *  * Neither the name of Opera Software nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 * DISCLAIMER OF WARRANTY
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

#define LANG_LIST_SIZE 8
#define HDR_MAXLEN 256
#define LANG_MAXLEN 8

struct language {
    char name[LANG_MAXLEN];
    float q;
    VTAILQ_ENTRY(language) list;
};

struct vmod_i18n {
    unsigned     magic;
#define VMOD_I18N_MAGIC  0x27032703
    char        *default_language;
    char        *supported_languages;
    VTAILQ_HEAD(, language) pl;
};

/* In-place lowercase of a string */
static void
strtolower(char *s)
{
    register char *c;
    for (c=s; *c; c++) {
        if (isupper(*c)) {
            *c = tolower(*c);
        }
    }
    return;
}


/* Unloading of the vmod private data structure */
static void
vmod_free(void *priv)
{
    struct vmod_i18n *v;
    struct language *l;

    if (priv) {
        CAST_OBJ_NOTNULL(v, priv, VMOD_I18N_MAGIC);

        if (v->default_language)
            free(v->default_language);

        if (v->supported_languages)
            free(v->supported_languages);

        while (! VTAILQ_EMPTY(&v->pl)) {
            l = VTAILQ_FIRST(&v->pl);
            VTAILQ_REMOVE(&v->pl, l, list);
        }

        FREE_OBJ(v);
    }
}


static struct vmod_i18n *
vmod_get(struct vmod_priv *priv)
{
    struct vmod_i18n *v;

    AN(priv);

    if (priv->priv) {
        CAST_OBJ_NOTNULL(v, priv->priv, VMOD_I18N_MAGIC);
    }
    else {
        ALLOC_OBJ(v, VMOD_I18N_MAGIC);
        AN(v);
        priv->free = vmod_free;
        priv->priv = v;
    }

    return (v);
}


int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf) {
    struct vmod_i18n *v;
    v = vmod_get(priv);
    return (0);
}


void
vmod_default_language(struct sess *sp, struct vmod_priv *priv,
    const char *def_lang)
{
    struct vmod_i18n *v;

    (void)sp;
    AN(priv);

    v = vmod_get(priv);
    v->default_language = strdup(def_lang);
    /* fprintf(stderr, "default_language: %s\n", v->default_language); */
}


void
vmod_supported_languages(struct sess *sp, struct vmod_priv *priv,
    const char *lang_list)
{
    struct vmod_i18n *v;
    char *p;
    unsigned u;

    (void)sp;
    AN(priv);

    v = vmod_get(priv);
    p = malloc(strlen(lang_list) + 3);

    if (p == NULL) {
        VSL(SLT_VCL_Log, 0, "i18n-vmod: no storage for supported_languages");
        return;
    }

    strcpy(p, ":");
    strcat(p, lang_list);
    strcat(p, ":");

    v->supported_languages = p;
    /*fprintf(stderr, "supported_languages: %s\n", v->supported_languages);*/
}


/* Checks if a given language is in the static list of the ones we support */
int
vmod_is_supported(struct sess *sp, struct vmod_priv *priv, const char *lang)
{
    struct vmod_i18n *v;
    const char *supported_languages;
    char *l;
    char match_str[LANG_MAXLEN + 3] = "";  /* :, :, \0 = 3 */
    int is_supported = 0;

    (void)sp;
    AN(priv);
    v = vmod_get(priv);
    AN(v);

    supported_languages = v->supported_languages;

    if (supported_languages == NULL) {
        VSL(SLT_VCL_Log, 0, "no supported_languages configuration");
        is_supported = 1;
    }
    else {
        /* We want to match 'zh-cn' and 'zh-CN' too */
        l = strdup(lang);
        AN(l);

        strtolower(l);

        /* Search ":<lang>:" in supported languages string */
        strncpy(match_str, ":", 1);
        strncat(match_str, l, LANG_MAXLEN);
        strncat(match_str, ":\0", 2);

        if (strstr(supported_languages, match_str))
            is_supported = 1;

        free(l);
    }

    return is_supported;
}

/* Used by qsort() below
static int
sort_by_q(const void *x, const void *y)
{
    struct lang_list *a = (struct lang_list *)x;
    struct lang_list *b = (struct lang_list *)y;
    if (a->q > b->q) return -1;
    if (a->q < b->q) return 1;
    return 0;
}
*/


void
push_lang(struct sess *sp, struct vmod_i18n *v, const char *name, float q)
{
    struct language *l;

    AN(sp);
    AN(v);

    l = (struct language *) WS_Alloc(sp->ws, sizeof(struct language));
    if (l == NULL) {
        VSL(SLT_VCL_Log, 0, "i18n-vmod: unable to get storage for language");
        return;
    }
    strncpy(l->name, name, LANG_MAXLEN); /*WS_Dup(sp->ws, name);*/

    if (q <= 0.0)
        q = 1.0;

    l->q = q;

    VTAILQ_INSERT_TAIL(&v->pl, l, list);
}


/* Reads Accept-Language, parses it, and finds the first match
   among the supported languages. In case of no match,
   returns the default language.
*/
void
vmod_parse(struct sess *sp, struct vmod_priv *priv,
           const char *accept_lang, const char *def)
{
    struct vmod_i18n *v;
    char *lang_tok = NULL;
    char root_lang[3];
    char *header;
    char header_copy[HDR_MAXLEN];
    char *pos = NULL;
    char *q_spec = NULL;
    unsigned int curr_lang = 0, i = 0;
    float q;
    struct language *newlang;

    AN(priv);
    v = vmod_get(priv);
    CHECK_OBJ_NOTNULL(v, VMOD_I18N_MAGIC);
        
    VSL(SLT_VCL_Log, 0, "i18n-vmod: parsing '%s'", accept_lang);

    VTAILQ_INIT(&v->pl);
    AN(&v->pl);

    /* If called twice during the same request,
       clean out old state before proceeding */
    while (! VTAILQ_EMPTY(&v->pl)) {
        newlang = VTAILQ_FIRST(&v->pl);
        VTAILQ_REMOVE(&v->pl, newlang, list);
    }

    if (accept_lang == NULL || strlen(accept_lang) == 0) {
        VSL(SLT_VCL_Log, 0, "i18n-vmod: nothing to parse");
        return;
    }

    if (strlen(accept_lang) + 1 >= HDR_MAXLEN) {
        VSL(SLT_VCL_Log, 0, "i18n-vmod: Accept-Language string too long");
        return;
    }

    /* Empty or default string, return default language immediately */
    if ((def != NULL) && (0 == strcmp(accept_lang, def))) {
        //push_lang(sp, v, def, 1.0);
        VSL(SLT_VCL_Log, 0, "i18n-vmod: default language received");
        return;
    }

    VSL(SLT_VCL_Log, 0, "i18n-vmod: strtok loop");

    /* Tokenize Accept-Language */
    header = strncpy(header_copy, accept_lang, sizeof(header_copy));

    while ((lang_tok = strtok_r(header, " ,", &pos))) {

        q = 1.0;

        if ((q_spec = strstr(lang_tok, ";q="))) {
            /* Truncate language name before ';' */
            *q_spec = '\0';
            /* Get q value */
            sscanf(q_spec + 3, "%f", &q);
        }

        /* Wildcard language '*' should be last in list */
        if ((*lang_tok) == '*')
            q = 0.0;

        VSL(SLT_VCL_Log, 0, "i18n-vmod: before vmod_is_supported");

        /* Push in the prioritized list */
        if (! vmod_is_supported(sp, priv, lang_tok)) {
            continue;
        }

        VSL(SLT_VCL_Log, 0, "i18n-vmod: after vmod_is_supported");

        push_lang(sp, v, lang_tok, q);
        i++;

        /* For cases like 'en-GB', we also want the root language in the final list */
        if ('-' == lang_tok[2]) {
            root_lang[0] = lang_tok[0];
            root_lang[1] = lang_tok[1];
            root_lang[2] = '\0';
            push_lang(sp, v, root_lang, q - 0.001);
        }

        /* For strtok_r() to proceed from where it left off */
        header = NULL;

        /* Break out if stored max no. of languages */
        if (curr_lang >= LANG_LIST_SIZE)
            break;
    }

    VSL(SLT_VCL_Log, 0, "i18n-vmod: parsed %i languages", i);

    /* TODO Sort by priority */
    /* qsort(pl, curr_lang, sizeof(struct lang_list), &sort_by_q); */

    /* Match with supported languages
    for (i = 0; i < curr_lang; i++) {
        if (vmod_is_supported(pl[i].lang))
            return pl[i].lang;
    }

    return def;
    */
}

/* Reads Accept-Language header (or any other string passed to it), and carries
   out Accept-Language parsing. The (language, q) pairs found are sorted by
   priority (q), and the first language that is also declared as supported
   (through the `supported_languages()' call, gets returned.

   If no languages are supported, all languages are assumed to be supported.

   If no default language has been specified, English is assumed ("en").
*/
const char *
vmod_match(struct sess *sp, struct vmod_priv *priv, const char *accept_lang)
{
    struct vmod_i18n *v;
    struct lang_list *pl;
    struct language *pref_lang;
    const char *pref_name;
    unsigned u, l;
    char *p;

    CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
    AN(priv);
    v = vmod_get(priv);

    /* Parse the input string for (language, q) pairs */
    vmod_parse(sp, priv, accept_lang, v->default_language);

    /* TODO filter languages for only the ones we support */

    /* First language in the resulting list is the one we pick */
    pref_lang = VTAILQ_FIRST(&v->pl);

    /* No language parsed, output the set default */
    if (pref_lang == NULL) {
        pref_name = v->default_language;
    }
    else {
        pref_name = pref_lang->name;
    }

    u = WS_Reserve(sp->ws, 0);
    p = sp->ws->f;
    l = snprintf(p, u, "%s", pref_name != NULL ? pref_name : "en");
    l++;

    if (l > u) {
        /* No workspace memory, reset and leave */
        WS_Release(sp->ws, 0);
        return (NULL);
    }

    WS_Release(sp->ws, l);
    return p;
}