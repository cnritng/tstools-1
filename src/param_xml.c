/* vim: set tabstop=8 shiftwidth=8: */
#include <string.h> /* for memcpy() only */
#include <float.h> /* for FLT_DIG, DBL_DIG, LDBL_DIG, etc */
#include <ctype.h> /* for isspace() */

#include "zlst.h"
#include "param_xml.h"

/* report level */
#define RPT_ERR (1) /* error, system error */
#define RPT_WRN (2) /* warning, maybe wrong, maybe OK */
#define RPT_INF (3) /* important information */
#define RPT_DBG (4) /* debug information */

/* report micro */
#define RPT(lvl, ...) do \
{ \
        if(lvl <= rpt_lvl) \
        { \
                switch(lvl) \
                { \
                        case RPT_ERR: fprintf(stderr, "%s: %d: err: ", __FILE__, __LINE__); break; \
                        case RPT_WRN: fprintf(stderr, "%s: %d: wrn: ", __FILE__, __LINE__); break; \
                        case RPT_INF: fprintf(stderr, "%s: %d: inf: ", __FILE__, __LINE__); break; \
                        case RPT_DBG: fprintf(stderr, "%s: %d: dbg: ", __FILE__, __LINE__); break; \
                        default:      fprintf(stderr, "%s: %d: ???: ", __FILE__, __LINE__); break; \
                } \
                fprintf(stderr, __VA_ARGS__); \
                fprintf(stderr, "\n"); \
        } \
} while (0)

static int rpt_lvl = RPT_WRN; /* report level: ERR, WRN, INF, DBG */

/* for SUPPORT_LONG_DOUBLE */
#ifdef DBL_DIG
#pragma message("has DBL_DIG")
#ifdef LDBL_DIG
#pragma message("has LDBL_DIG")
#if DBL_DIG != LDBL_DIG /* in system like VxWorks5.5, "double" is equal to "long double" */
#pragma message("DBL_DIG != LDBL_DIG")
#ifdef HAVE_STRTOLD
#pragma message("HAVE_STRTOLD: param_xml will support long double")
#define SUPPORT_LONG_DOUBLE
#else
#pragma message("ndef HAVE_STRTOLD: param_xml will NOT support long double")
#endif
#else
#pragma message("DBL_DIG == LDBL_DIG")
#endif
#else
#pragma message("no LDBL_DIG")
#endif
#else
#pragma message("no DBL_DIG")
#endif

#if 1 /* FIXME: should be 1 to support dynamic buffer */
#define MORE_IDX /* more "idx" attribute in XML */
#endif

#define CTNT_LENGTH (512) /* size of str_ctnt[] */
#define DATA_SPACE (' ') /* space char for PT_SINT, PT_UINT and PT_FLOT */

static const xmlChar xStrIdx[] = "idx";
static const xmlChar xStrTyp[] = "typ";
static const xmlChar xStrCnt[] = "cnt";

static int sint2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int uint2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int flot2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int stri2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int enum2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int stru2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int list2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int vlst2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);

static int xml2sint(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int xml2uint(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int xml2flot(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int xml2stri(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int xml2enum(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int xml2stru(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int xml2list(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);
static int xml2vlst(void *mem_base, xmlNode *xnode, struct pdesc *pdesc);

/* for speed reason, define sint2str micro and uint2str micro here */
#define UINT64_MAX_DEC_LENGTH 20 /* UINT64_MAX is 1.8e+19 level */

#define sint2str(STR, sdat, type, i, str) \
        do { \
                type udat; \
                \
                /* sign */ \
                if(sdat < 0) { \
                        *STR++ = '-'; \
                        sdat = -sdat; /* INT_MIN = -INT_MIN */ \
                } \
                udat = *((type *)&sdat); \
                \
                /* convert into str[] */ \
                for(i = 0; i < UINT64_MAX_DEC_LENGTH; i++) { \
                        str[i] = (char)(udat % 10) + '0'; \
                        udat /= 10; \
                        if(!udat) {break;} \
                } \
                \
                /* move into STR[] */ \
                for(; i >= 0; i--) { \
                        *STR++ = str[i]; \
                } \
                *STR = '\0'; \
        } while(0)

#define uint2str(STR, udat, FORMAT, i, str) \
        do { \
                /* convert into str[], optimized with 3-case */ \
                switch(FORMAT) { \
                case PT_FMT_x: \
                        *STR++ = '0'; \
                        *STR++ = 'x'; \
                        for(i = 0; i < UINT64_MAX_DEC_LENGTH; i++) { \
                                str[i] = (char)(udat & 0x0F); \
                                str[i] += ((str[i] <= 9) ?  '0' : ('a' - 10)); \
                                udat >>= 4; \
                                if(!udat) {break;} \
                        } \
                break; \
                case PT_FMT_X: \
                        *STR++ = '0'; \
                        *STR++ = 'x'; \
                        for(i = 0; i < UINT64_MAX_DEC_LENGTH; i++) { \
                                str[i] = (char)(udat & 0x0F); \
                                str[i] += ((str[i] <= 9) ?  '0' : ('A' - 10)); \
                                udat >>= 4; \
                                if(!udat) {break;} \
                        } \
                break; \
                default: /* PT_FMT_u or others */ \
                        for(i = 0; i < UINT64_MAX_DEC_LENGTH; i++) { \
                                str[i] = (char)(udat % 10) + '0'; \
                                udat /= 10; \
                                if(!udat) {break;} \
                        } \
                break; \
                } \
                \
                /* move into STR[] */ \
                for(; i >= 0; i--) { \
                        *STR++ = str[i]; \
                } \
                *STR = '\0'; \
        } while(0)

#if 0
#ifndef INTMAX_MAX
typedef int64_t intmax_t;
typedef uint64_t uintmax_t;
#endif /* !INTMAX_MAX */

static const intmax_t intmax_max = +9223372036854775807LL;
static const uintmax_t uintmax_imax = +9223372036854775807ULL;
static const uintmax_t uintmax_imin = +9223372036854775808ULL;
static const uintmax_t uintmax_umax = 18446744073709551615ULL;

static  intmax_t strtoimax(const char *nptr, char **endptr, int base);
static uintmax_t strtoumax(const char *nptr, char **endptr, int base);
#endif

static int sint2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);
static int node2sint(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);

static int uint2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);
static int node2uint(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);

static int flot2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);
static int node2flot(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);

static int stru2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);
static int node2stru(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size);

/* module interface */
int param2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        struct pdesc *cur_pdesc;

        for(cur_pdesc = pdesc; cur_pdesc->type; cur_pdesc++) {
                if(cur_pdesc->toxml) {
                        RPT(RPT_INF, "param2xml: toxml");
                        cur_pdesc->toxml(mem_base, xnode, cur_pdesc);
                        continue;
                }
                switch(PT_TYP(cur_pdesc->type)) {
                        case PT_TYP_SINT: sint2xml(mem_base, xnode, cur_pdesc); break;
                        case PT_TYP_UINT: uint2xml(mem_base, xnode, cur_pdesc); break;
                        case PT_TYP_FLOT: flot2xml(mem_base, xnode, cur_pdesc); break;
                        case PT_TYP_STRI: stri2xml(mem_base, xnode, cur_pdesc); break;
                        case PT_TYP_ENUM: enum2xml(mem_base, xnode, cur_pdesc); break;
                        case PT_TYP_STRU: stru2xml(mem_base, xnode, cur_pdesc); break;
                        case PT_TYP_LIST: list2xml(mem_base, xnode, cur_pdesc); break;
                        case PT_TYP_VLST: vlst2xml(mem_base, xnode, cur_pdesc); break;
                        default: RPT(RPT_INF, "param2xml: bad type(0x%X)", cur_pdesc->type); break;
                }
        }
        return 0;
}

int xml2param(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        xmlNode *sub_xnode;
        struct pdesc *cur_pdesc;

        /* clear pdesc->index */
        /* FIXME: if the pdesc array be used in subnode of the xnode, pdesc->index will be wrong! */
        for(cur_pdesc = pdesc; cur_pdesc->type; cur_pdesc++) {
                cur_pdesc->index = 0;
        }

        for(sub_xnode = xnode->xmlChildrenNode; sub_xnode; sub_xnode = sub_xnode->next) {

                /* search cur_pdesc */
                for(cur_pdesc = pdesc; cur_pdesc->type; cur_pdesc++) {
                        if(xmlStrEqual((xmlChar *)(cur_pdesc->name), sub_xnode->name)) {
                                break;
                        }
                }
                if(!(cur_pdesc->type)) {
                        /* fprintf(stderr, "no 0x%X->%s\n", (int)sub_xnode, (char *)sub_xnode->name); */
                        /* FIXME: why many sub_xnode has name "text"? */
                        continue;
                }

                if(cur_pdesc->xmlto) {
                        RPT(RPT_INF, "xml2param: xmlto");
                        cur_pdesc->xmlto(mem_base, sub_xnode, cur_pdesc);
                        continue;
                }
                switch(PT_TYP(cur_pdesc->type)) {
                        case PT_TYP_SINT: xml2sint(mem_base, sub_xnode, cur_pdesc); break;
                        case PT_TYP_UINT: xml2uint(mem_base, sub_xnode, cur_pdesc); break;
                        case PT_TYP_FLOT: xml2flot(mem_base, sub_xnode, cur_pdesc); break;
                        case PT_TYP_STRI: xml2stri(mem_base, sub_xnode, cur_pdesc); break;
                        case PT_TYP_ENUM: xml2enum(mem_base, sub_xnode, cur_pdesc); break;
                        case PT_TYP_STRU: xml2stru(mem_base, sub_xnode, cur_pdesc); break;
                        case PT_TYP_LIST: xml2list(mem_base, sub_xnode, cur_pdesc); break;
                        case PT_TYP_VLST: xml2vlst(mem_base, sub_xnode, cur_pdesc); break;
                        default: RPT(RPT_INF, "xml2param: bad type(0x%X)", cur_pdesc->type); break;
                }
        }
        return 0;
}

/* subfunctions */
static int sint2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;

        RPT(RPT_INF, "sint2xml: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlNode *sub_xnode;
                struct adesc *adesc;
                int *cob; /* count of buffer */

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "sint2xml: bad adesc");
                        return -1;
                }
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);

                for(i = 0; i < count; i++, mem += pdesc->size, cob++) {
                        if(0 == *cob) {
                                RPT(RPT_INF, "sint2xml: count of buffer is zero");
                                continue;
                        }

                        sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                        if(!sub_xnode) {
                                RPT(RPT_ERR, "sint2xml: bad sub node");
                                return -1;
                        }

#ifdef MORE_IDX
                        if(i) {
                                char str_idx[10];

                                sprintf(str_idx, "%d", i);
                                xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                        }
#endif
                        {
                                char str_cnt[10];

                                sprintf(str_cnt, "%d", *cob);
                                xmlNewProp(sub_xnode, xStrCnt, (const xmlChar *)str_cnt);
                        }

                        RPT(RPT_INF, "sint2xml: cob %d, size: %zd", *cob, adesc->size);
                        sint2node(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                        xmlAddChild(xnode, sub_xnode);
                }
        }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "sint2xml: coa %d, size: %zd", count, pdesc->size);
                sint2node(mem, xnode, pdesc, count, pdesc->size);
        }
        return 0;
}

static int uint2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;

        RPT(RPT_INF, "uint2xml: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlNode *sub_xnode;
                struct adesc *adesc;
                int *cob; /* count of buffer */

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "uint2xml: bad adesc");
                        return -1;
                }
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);

                for(i = 0; i < count; i++, mem += pdesc->size, cob++) {
                        if(0 == *cob) {
                                RPT(RPT_INF, "uint2xml: count of buffer is zero");
                                continue;
                        }

                        sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                        if(!sub_xnode) {
                                RPT(RPT_ERR, "uint2xml: bad sub node");
                                return -1;
                        }

#ifdef MORE_IDX
                        if(i) {
                                char str_idx[10];

                                sprintf(str_idx, "%d", i);
                                xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                        }
#endif
                        {
                                char str_cnt[10];

                                sprintf(str_cnt, "%d", *cob);
                                xmlNewProp(sub_xnode, xStrCnt, (const xmlChar *)str_cnt);
                        }

                        RPT(RPT_INF, "uint2xml: cob %d, size: %zd", *cob, adesc->size);
                        uint2node(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                        xmlAddChild(xnode, sub_xnode);
                }
        }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "uint2xml: coa %d, size: %zd", count, pdesc->size);
                uint2node(mem, xnode, pdesc, count, pdesc->size);
        }
        return 0;
}

static int flot2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;

        RPT(RPT_INF, "flot2xml: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlNode *sub_xnode;
                struct adesc *adesc;
                int *cob; /* count of buffer */

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "flot2xml: bad adesc");
                        return -1;
                }
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);

                for(i = 0; i < count; i++, mem += pdesc->size, cob++) {
                        if(0 == *cob) {
                                RPT(RPT_INF, "flot2xml: count of buffer is zero");
                                continue;
                        }

                        sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                        if(!sub_xnode) {
                                RPT(RPT_ERR, "flot2xml: bad sub node");
                                return -1;
                        }

#ifdef MORE_IDX
                        if(i) {
                                char str_idx[10];

                                sprintf(str_idx, "%d", i);
                                xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                        }
#endif
                        {
                                char str_cnt[10];

                                sprintf(str_cnt, "%d", *cob);
                                xmlNewProp(sub_xnode, xStrCnt, (const xmlChar *)str_cnt);
                        }

                        RPT(RPT_INF, "flot2xml: cob %d, size: %zd", *cob, adesc->size);
                        flot2node(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                        xmlAddChild(xnode, sub_xnode);
                }
        }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "flot2xml: coa %d, size: %zd", count, pdesc->size);
                flot2node(mem, xnode, pdesc, count, pdesc->size);
        }
        return 0;
}

static int stri2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;
        xmlNode *sub_xnode;

        RPT(RPT_INF, "stri2xml: \"%s\"", pdesc->name);
        for(i = 0; i < count; i++, mem += pdesc->size) {
                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "stri2xml: bad sub node");
                        return -1;
                }

#ifdef MORE_IDX
                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }
#endif

                RPT(RPT_INF, "stri2xml: %s", mem);
                xmlNodeAddContent(sub_xnode, (const xmlChar *)mem);
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int enum2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;
        xmlNode *sub_xnode;
        struct enume *enum_item;

        RPT(RPT_INF, "enum2xml: \"%s\"", pdesc->name);
        for(i = 0; i < count; i++, mem += pdesc->size) {
                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "enum2xml: bad sub node");
                        return -1;
                }

#ifdef MORE_IDX
                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }
#endif

                /* search value in struct enume array */
                for(enum_item = (struct enume *)(pdesc->aux); enum_item->key; enum_item++) {
                        if(*((int *)mem) == enum_item->value) {
                                break;
                        }
                }
                if(!enum_item->key) {
                        enum_item = (struct enume *)(pdesc->aux);
                }

                RPT(RPT_INF, "enum2xml: %s", enum_item->key);
                xmlNodeAddContent(sub_xnode, (const xmlChar *)(enum_item->key));
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int stru2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;

        RPT(RPT_INF, "stru2xml: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlNode *sub_xnode;
                struct adesc *adesc;
                int *cob; /* count of buffer */

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "stru2xml: bad adesc");
                        return -1;
                }
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);

                for(i = 0; i < count; i++, mem += pdesc->size, cob++) {
                        if(0 == *cob) {
                                RPT(RPT_INF, "stru2xml: count of buffer is zero");
                                continue;
                        }

                        sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                        if(!sub_xnode) {
                                RPT(RPT_ERR, "stru2xml: bad sub node");
                                return -1;
                        }

#ifdef MORE_IDX
                        if(i) {
                                char str_idx[10];

                                sprintf(str_idx, "%d", i);
                                xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                        }
#endif
                        {
                                char str_cnt[10];

                                sprintf(str_cnt, "%d", *cob);
                                xmlNewProp(sub_xnode, xStrCnt, (const xmlChar *)str_cnt);
                        }

                        RPT(RPT_INF, "stru2xml: cob %d, size: %zd", *cob, adesc->size);
                        stru2node(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                        xmlAddChild(xnode, sub_xnode);
                }
         }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "stru2xml: coa %d, size: %zd", count, pdesc->size);
                stru2node(mem, xnode, pdesc, count, pdesc->size);
        }
        return 0;
}

static int list2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;
        xmlNode *sub_xnode;

        RPT(RPT_INF, "list2xml: \"%s\"", pdesc->name);
        for(i = 0; i < count; i++, mem += pdesc->size) {
                int sub_i;
                struct znode *list;

                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "list2xml: bad sub node");
                        return -1;
                }

#ifdef MORE_IDX
                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }
#endif

                RPT(RPT_INF, "list2xml[%d]:", i);
                for(list = *(struct znode **)mem, sub_i = 0; list; list = list->next, sub_i++) {
                        xmlNode *sub_sub_xnode;
                        struct adesc *adesc;

                        adesc = (struct adesc *)(pdesc->aux);
                        if(!adesc) {
                                RPT(RPT_WRN, "list2xml: bad adesc");
                                continue;
                        }

                        sub_sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                        if(!sub_sub_xnode) {
                                RPT(RPT_ERR, "list2xml: bad sub sub node");
                                return -1;
                        }

#ifdef MORE_IDX
                        if(sub_i) {
                                char str_idx[10];

                                sprintf(str_idx, "%d", sub_i);
                                xmlNewProp(sub_sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                        }
#endif

                        RPT(RPT_INF, "node[%d]:", sub_i);
                        param2xml(list, sub_sub_xnode, adesc->pdesc);
                        xmlAddChild(sub_xnode, sub_sub_xnode);
                }
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int vlst2xml(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        int i;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        int count = pdesc->count;
        xmlNode *sub_xnode;

        RPT(RPT_INF, "vlst2xml: \"%s\"", pdesc->name);
        for(i = 0; i < count; i++, mem += pdesc->size) {
                int sub_i;
                struct znode *list;

                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "vlst2xml: bad sub node");
                        return -1;
                }

#ifdef MORE_IDX
                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }
#endif

                RPT(RPT_INF, "vlst2xml[%d]:", i);
                for(list = *(struct znode **)mem, sub_i = 0; list; list = list->next, sub_i++) {
                        xmlNode *sub_sub_xnode;
                        struct adesc *adesc;

                        /* search in adesc array */
                        if(!(pdesc->aux)) {
                                RPT(RPT_WRN, "vlst2xml: bad adesc");
                                continue;
                        }
                        for(adesc = (struct adesc *)(pdesc->aux); adesc->name; adesc++) {
                                if(xmlStrEqual((xmlChar *)(adesc->name),
                                               (xmlChar *)(list->name))) {
                                        break;
                                }
                        }
                        if(!(adesc->name)) {
                                adesc = (struct adesc *)(pdesc->aux);
                        }

                        /* got adesc */
                        sub_sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                        if(!sub_sub_xnode) {
                                RPT(RPT_ERR, "vlst2xml: bad sub sub node");
                                return -1;
                        }

#ifdef MORE_IDX
                        if(sub_i) {
                                char str_idx[10];

                                sprintf(str_idx, "%d", sub_i);
                                xmlNewProp(sub_sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                        }
#endif

                        RPT(RPT_INF, "node[%d]:", sub_i);
                        xmlNewProp(sub_sub_xnode, xStrTyp, (const xmlChar *)list->name);
                        param2xml(list, sub_sub_xnode, adesc->pdesc);
                        xmlAddChild(sub_xnode, sub_sub_xnode);
                }
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int xml2sint(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;

        RPT(RPT_INF, "xml2sint: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlChar *idx;
                xmlChar *cnt;
                int *cob; /* count of buffer */
                struct adesc *adesc;

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "xml2sint: bad adesc");
                        return -1;
                }

                /* adjust pdesc->index, calc mem */
                idx = xmlGetProp(xnode, xStrIdx);
                if(idx) {
                        pdesc->index = atoi((char *)idx);
                        xmlFree(idx);
                }
                if(pdesc->index >= pdesc->count) {
                        RPT(RPT_WRN, "xml2sint: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                        return -1;
                }
                mem += (pdesc->index * pdesc->size);

                /* get count of buffer */
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);
                cob += pdesc->index;
                cnt = xmlGetProp(xnode, xStrCnt);
                if(!cnt) {
                        RPT(RPT_ERR, "xml2sint: no count of buffer");
                        return -1;
                }
                *cob = atoi((char *)cnt);
                xmlFree(cnt);
                if(0 == *cob) {
                        RPT(RPT_ERR, "xml2sint: count of buffer is zero");
                        return -1;
                }

                RPT(RPT_INF, "xml2sint: xmlMalloc: %d * %zd", *cob, adesc->size);
                *((void **)mem) = xmlMalloc(*cob * adesc->size);
                if(!*((void **)mem)) {
                        RPT(RPT_ERR, "xml2sint: malloc failed");
                        return -1;
                }

                pdesc->index = 0;
                for(xmlNode *sub_xnode = xnode->xmlChildrenNode; sub_xnode; sub_xnode = sub_xnode->next) {
                        if(!xmlStrEqual((xmlChar *)(pdesc->name), sub_xnode->name)) {
                                /* FIXME: why many sub_xnode has name "text"? */
                                continue;
                        }
                        RPT(RPT_INF, "xml2sint: cob %d, size %zd", *cob, adesc->size);
                        node2sint(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                }
        }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "xml2sint: coa %d, size %zd", pdesc->count, pdesc->size);
                node2sint(mem, xnode, pdesc, pdesc->count, pdesc->size);
        }
        return 0;
}

static int xml2uint(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;

        RPT(RPT_INF, "xml2uint: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlChar *idx;
                xmlChar *cnt;
                int *cob; /* count of buffer */
                struct adesc *adesc;

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "xml2uint: bad adesc");
                        return -1;
                }

                /* adjust pdesc->index, calc mem */
                idx = xmlGetProp(xnode, xStrIdx);
                if(idx) {
                        pdesc->index = atoi((char *)idx);
                        xmlFree(idx);
                }
                if(pdesc->index >= pdesc->count) {
                        RPT(RPT_WRN, "xml2uint: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                        return -1;
                }
                mem += (pdesc->index * pdesc->size);

                /* get count of buffer */
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);
                cob += pdesc->index;
                cnt = xmlGetProp(xnode, xStrCnt);
                if(!cnt) {
                        RPT(RPT_ERR, "xml2uint: no count of buffer");
                        return -1;
                }
                *cob = atoi((char *)cnt);
                xmlFree(cnt);
                if(0 == *cob) {
                        RPT(RPT_ERR, "xml2uint: count of buffer is zero");
                        return -1;
                }

                RPT(RPT_INF, "xml2uint: xmlMalloc: %d * %zd", *cob, adesc->size);
                *((void **)mem) = xmlMalloc(*cob * adesc->size);
                if(!*((void **)mem)) {
                        RPT(RPT_ERR, "xml2uint: malloc failed");
                        return -1;
                }

                pdesc->index = 0;
                for(xmlNode *sub_xnode = xnode->xmlChildrenNode; sub_xnode; sub_xnode = sub_xnode->next) {
                        if(!xmlStrEqual((xmlChar *)(pdesc->name), sub_xnode->name)) {
                                /* FIXME: why many sub_xnode has name "text"? */
                                continue;
                        }
                        RPT(RPT_INF, "xml2uint: cob %d, size %zd", *cob, adesc->size);
                        node2uint(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                }
        }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "xml2uint: coa %d, size %zd", pdesc->count, pdesc->size);
                node2uint(mem, xnode, pdesc, pdesc->count, pdesc->size);
        }
        return 0;
}

static int xml2flot(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;

        RPT(RPT_INF, "xml2flot: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlChar *idx;
                xmlChar *cnt;
                int *cob; /* count of buffer */
                struct adesc *adesc;

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "xml2flot: bad adesc");
                        return -1;
                }

                /* adjust pdesc->index, calc mem */
                idx = xmlGetProp(xnode, xStrIdx);
                if(idx) {
                        pdesc->index = atoi((char *)idx);
                        xmlFree(idx);
                }
                if(pdesc->index >= pdesc->count) {
                        RPT(RPT_WRN, "xml2flot: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                        return -1;
                }
                mem += (pdesc->index * pdesc->size);

                /* get count of buffer */
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);
                cob += pdesc->index;
                cnt = xmlGetProp(xnode, xStrCnt);
                if(!cnt) {
                        RPT(RPT_ERR, "xml2flot: no count of buffer");
                        return -1;
                }
                *cob = atoi((char *)cnt);
                xmlFree(cnt);
                if(0 == *cob) {
                        RPT(RPT_ERR, "xml2flot: count of buffer is zero");
                        return -1;
                }

                RPT(RPT_INF, "xml2flot: xmlMalloc: %d * %zd", *cob, adesc->size);
                *((void **)mem) = xmlMalloc(*cob * adesc->size);
                if(!*((void **)mem)) {
                        RPT(RPT_ERR, "xml2flot: malloc failed");
                        return -1;
                }

                pdesc->index = 0;
                for(xmlNode *sub_xnode = xnode->xmlChildrenNode; sub_xnode; sub_xnode = sub_xnode->next) {
                        if(!xmlStrEqual((xmlChar *)(pdesc->name), sub_xnode->name)) {
                                /* FIXME: why many sub_xnode has name "text"? */
                                continue;
                        }
                        RPT(RPT_INF, "xml2flot: cob %d, size %zd", *cob, adesc->size);
                        node2flot(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                }
        }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "xml2flot: coa %d, size %zd", pdesc->count, pdesc->size);
                node2flot(mem, xnode, pdesc, pdesc->count, pdesc->size);
        }
        return 0;
}

static int xml2stri(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        xmlChar *idx;
        xmlChar *ctnt;
        int len;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;

        RPT(RPT_INF, "xml2stri: \"%s\"", pdesc->name);

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= pdesc->count) {
                RPT(RPT_INF, "xml2stri: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                return -1;
        }
        mem += (pdesc->index * pdesc->size);

        /* get string */
        ctnt = xmlNodeGetContent(xnode);
        if(!ctnt) {
                RPT(RPT_ERR, "xml2stri: no content");
                return -1;
        }
        len = xmlStrlen(ctnt) + 1; /* with '\0' */
        len = ((len > pdesc->size) ? pdesc->size : len);
        memcpy(mem, ctnt, len);
        xmlFree(ctnt);
        RPT(RPT_INF, "xml2stri[%d]: %s", pdesc->index, (char *)mem);
        pdesc->index++;
        return 0;
}

static int xml2enum(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        xmlChar *idx;
        xmlChar *ctnt;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        struct enume *enum_item;

        RPT(RPT_INF, "xml2enum: \"%s\"", pdesc->name);

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= pdesc->count) {
                RPT(RPT_INF, "xml2enum: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                return -1;
        }
        mem += (pdesc->index * pdesc->size);

        /* search key in struct enume array */
        ctnt = xmlNodeGetContent(xnode);
        if(!ctnt) {
                RPT(RPT_ERR, "xml2enum: no content");
                return -1;
        }
        for(enum_item = (struct enume *)(pdesc->aux); enum_item->key; enum_item++) {
                if(xmlStrEqual(ctnt, (xmlChar *)(enum_item->key))) {
                        break;
                }
        }
        xmlFree(ctnt);

        /* get enum */
        *(int *)mem = enum_item->value;
        RPT(RPT_INF, "xml2enum[%d]: %d(\"%s\")", pdesc->index, *(int *)mem, enum_item->key);
        pdesc->index++;
        return 0;
}

static int xml2stru(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;

        RPT(RPT_INF, "xml2stru: \"%s\"", pdesc->name);
        if(PT_ACS_X == PT_ACS(pdesc->type)) {
                xmlChar *idx;
                xmlChar *cnt;
                int *cob; /* count of buffer */
                struct adesc *adesc;

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "xml2stru: bad adesc");
                        return -1;
                }

                /* adjust pdesc->index, calc mem */
                idx = xmlGetProp(xnode, xStrIdx);
                if(idx) {
                        pdesc->index = atoi((char *)idx);
                        xmlFree(idx);
                }
                if(pdesc->index >= pdesc->count) {
                        RPT(RPT_WRN, "xml2stru: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                        return -1;
                }
                mem += (pdesc->index * pdesc->size);

                /* get count of buffer */
                cob = (int *)((uint8_t *)mem_base + adesc->boffset);
                cob += pdesc->index;
                cnt = xmlGetProp(xnode, xStrCnt);
                if(!cnt) {
                        RPT(RPT_ERR, "xml2stru: no count of buffer");
                        return -1;
                }
                *cob = atoi((char *)cnt);
                xmlFree(cnt);
                if(0 == *cob) {
                        RPT(RPT_ERR, "xml2stru: count of buffer is zero");
                        return -1;
                }

                RPT(RPT_INF, "xml2stru: xmlMalloc: %d * %zd", *cob, adesc->size);
                *((void **)mem) = xmlMalloc(*cob * adesc->size);
                if(!*((void **)mem)) {
                        RPT(RPT_ERR, "xml2stru: malloc failed");
                        return -1;
                }

                pdesc->index = 0;
                for(xmlNode *sub_xnode = xnode->xmlChildrenNode; sub_xnode; sub_xnode = sub_xnode->next) {
                        if(!xmlStrEqual((xmlChar *)(pdesc->name), sub_xnode->name)) {
                                /* FIXME: why many sub_xnode has name "text"? */
                                continue;
                        }
                        RPT(RPT_INF, "xml2stru: cob %d, size %zd", *cob, adesc->size);
                        node2stru(*((void **)mem), sub_xnode, pdesc, *cob, adesc->size);
                }
        }
        else { /* PT_ACS_S */
                RPT(RPT_INF, "xml2stru: coa %d, size %zd", pdesc->count, pdesc->size);
                node2stru(mem, xnode, pdesc, pdesc->count, pdesc->size);
        }
        return 0;
}

static int xml2list(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        xmlChar *idx;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        struct znode *list;
        xmlNode *sub_xnode;

        RPT(RPT_INF, "xml2list: \"%s\"", pdesc->name);

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= pdesc->count) {
                RPT(RPT_INF, "xml2list: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                return -1;
        }
        mem += (pdesc->index * pdesc->size);

        /* get list */
        RPT(RPT_INF, "xml2list[%d]:", pdesc->index);
        pdesc->index++;
        if(*(struct znode **)mem) {
                RPT(RPT_ERR, "xml2list: not an empty list");
                return -1;
        };
        for(sub_xnode = xnode->xmlChildrenNode; sub_xnode; sub_xnode = sub_xnode->next) {
                struct adesc *adesc;

                if(!xmlStrEqual((xmlChar *)(pdesc->name), sub_xnode->name)) {
                        /* FIXME: why many sub_xnode has name "text"? */
                        continue;
                }
                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc || !(adesc->name)) {
                        RPT(RPT_WRN, "xml2list: bad adesc");
                        continue;
                }

                /* add list node with adesc */
                list = (struct znode *)xmlMalloc(adesc->size);
                if(!list) {
                        RPT(RPT_INF, "xml2list: malloc znode failed");
                        continue;
                }
                memset(list, 0, adesc->size);
                zlst_push(mem, list);
                xml2param(list, sub_xnode, adesc->pdesc);
        }

        return 0;
}

static int xml2vlst(void *mem_base, xmlNode *xnode, struct pdesc *pdesc)
{
        xmlChar *idx;
        uint8_t *mem = (uint8_t *)mem_base + pdesc->offset;
        struct znode *list;
        xmlNode *sub_xnode;

        RPT(RPT_INF, "xml2vlst: \"%s\"", pdesc->name);

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= pdesc->count) {
                RPT(RPT_INF, "xml2vlst: idx(%d) >= count(%d), ignore", pdesc->index, pdesc->count);
                return -1;
        }
        mem += (pdesc->index * pdesc->size);

        /* get vlst */
        RPT(RPT_INF, "xml2vlst[%d]:", pdesc->index);
        pdesc->index++;
        if(*(struct znode **)mem) {
                RPT(RPT_ERR, "xml2vlst: not an empty list");
                return -1;
        };
        for(sub_xnode = xnode->xmlChildrenNode; sub_xnode; sub_xnode = sub_xnode->next) {
                struct adesc *adesc;
                xmlChar *type_name;

                if(!xmlStrEqual((xmlChar *)(pdesc->name), sub_xnode->name)) {
                        /* FIXME: why many sub_xnode has name "text"? */
                        continue;
                }

                /* search in adesc array */
                type_name = xmlGetProp(sub_xnode, xStrTyp);
                if(!type_name) {
                        RPT(RPT_ERR, "xml2vlst: type name");
                        continue;
                }
                if(!(pdesc->aux)) {
                        RPT(RPT_ERR, "xml2vlst: bad adesc");
                        continue;
                }
                for(adesc = (struct adesc *)(pdesc->aux); adesc->name; adesc++) {
                        if(xmlStrEqual((xmlChar *)(adesc->name), type_name)) {
                                break;
                        }
                }
                xmlFree(type_name);
                if(!(adesc->name)) {
                        continue;
                }

                /* add list node with adesc */
                list = (struct znode *)xmlMalloc(adesc->size);
                if(!list) {
                        RPT(RPT_INF, "xml2vlst: malloc znode failed");
                        continue;
                }
                memset(list, 0, adesc->size);
                zlst_set_name(list, adesc->name);
                zlst_push(mem, list);
                xml2param(list, sub_xnode, adesc->pdesc);
        }

        return 0;
}

static int sint2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        int i;
        char *p;
        xmlNode *sub_xnode;
        char str_ctnt[CTNT_LENGTH];
        int data_str_idx; /* in uint2str micro: index of data_str */
        char data_str[UINT64_MAX_DEC_LENGTH]; /* in uint2str micro: string of one data */

        i = 0;
        while(i < count) {
                int n; /* n-data per line */

                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "sint2node: no sub_xnode");
                        return -1;
                }

                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }

                switch(size) {
                        case sizeof(int8_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 16); i++, n++) {
                                        int8_t  sdat = *(((int8_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        sint2str(p, sdat, uint8_t, data_str_idx, data_str);
                                }
                                break;
                        case sizeof(int16_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 16); i++, n++) {
                                        int16_t  sdat = *(((int16_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        sint2str(p, sdat, uint16_t, data_str_idx, data_str);
                                }
                                break;
                        case sizeof(int32_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 8); i++, n++) {
                                        int32_t  sdat = *(((int32_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        sint2str(p, sdat, uint32_t, data_str_idx, data_str);
                                }
                                break;
                        case sizeof(int64_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 4); i++, n++) {
                                        int64_t  sdat = *(((int64_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        sint2str(p, sdat, uint64_t, data_str_idx, data_str);
                                }
                                break;
                        default:
                                sprintf(str_ctnt, "unsupported data size(%zd)", size);
                                i = count; /* stop converter */
                                break;
                }

                RPT(RPT_INF, "sint2node: %s", str_ctnt);
                xmlNodeAddContent(sub_xnode, (const xmlChar *)str_ctnt);
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int uint2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        int i;
        char *p;
        char fmt = (char)PT_FMT(pdesc->type);
        xmlNode *sub_xnode;
        char str_ctnt[CTNT_LENGTH];
        int data_str_idx; /* in uint2str micro: index of data_str */
        char data_str[UINT64_MAX_DEC_LENGTH]; /* in uint2str micro: string of one data */

        i = 0;
        while(i < count) {
                int n; /* n-data per line */

                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "uint2node: no sub_xnode");
                        return -1;
                }

                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }

                switch(size) {
                        case sizeof(uint8_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 16); i++, n++) {
                                        uint8_t udat = *(((uint8_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        uint2str(p, udat, fmt, data_str_idx, data_str);
                                }
                                break;
                        case sizeof(uint16_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 16); i++, n++) {
                                        uint16_t udat = *(((uint16_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        uint2str(p, udat, fmt, data_str_idx, data_str);
                                }
                                break;
                        case sizeof(uint32_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 8); i++, n++) {
                                        uint32_t udat = *(((uint32_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        uint2str(p, udat, fmt, data_str_idx, data_str);
                                }
                                break;
                        case sizeof(uint64_t):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 4); i++, n++) {
                                        uint64_t udat = *(((uint64_t *)mem) + i);
                                        if(n) {*p++ = DATA_SPACE;}
                                        uint2str(p, udat, fmt, data_str_idx, data_str);
                                }
                                break;
                        default:
                                sprintf(str_ctnt, "unsupported data size(%d)", size);
                                i = count; /* stop converter */
                                break;
                }

                RPT(RPT_INF, "uint2node: %s", str_ctnt);
                xmlNodeAddContent(sub_xnode, (const xmlChar *)str_ctnt);
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int flot2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        int i;
        char *p;
        xmlNode *sub_xnode;
        char str_ctnt[CTNT_LENGTH];

        i = 0;
        while(i < count) {
                int n; /* n-data per line */

                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "flot2node: no sub_xnode");
                        return -1;
                }

                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }

                switch(size) {
                        case sizeof(float):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 8); i++, n++) {
                                        if(n) {*p++ = DATA_SPACE;}
                                        p += sprintf(p, "%.*g", FLT_DIG, *(((float *)mem) + i));
                                }
                                break;
                        case sizeof(double):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 4); i++, n++) {
                                        if(n) {*p++ = DATA_SPACE;}
                                        p += sprintf(p, "%.*g", DBL_DIG, *(((double *)mem) + i));
                                }
                                break;
#ifdef SUPPORT_LONG_DOUBLE
                        case sizeof(long double):
                                for(p = str_ctnt, n = 0; (i < count) && (n < 4); i++, n++) {
                                        if(n) {*p++ = DATA_SPACE;}
                                        p += sprintf(p, "%.*Lg", LDBL_DIG, *(((long double *)mem) + i));
                                }
                                break;
#endif
                        default:
                                sprintf(str_ctnt, "unsupported data size(%zd)", size);
                                i = count; /* stop converter */
                                break;
                }

                RPT(RPT_INF, "flot2node: %s", str_ctnt);
                xmlNodeAddContent(sub_xnode, (const xmlChar *)str_ctnt);
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int stru2node(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        int i;

        for(i = 0; i < count; i++, mem += size) {
                xmlNode *sub_xnode;
                struct adesc *adesc;

                adesc = (struct adesc *)(pdesc->aux);
                if(!adesc) {
                        RPT(RPT_ERR, "stru2node: bad adesc");
                        return -1;
                }
                sub_xnode = xmlNewNode(NULL, (const xmlChar *)pdesc->name);
                if(!sub_xnode) {
                        RPT(RPT_ERR, "stru2node: no sub_xnode");
                        return -1;
                }

#ifdef MORE_IDX
                if(i) {
                        char str_idx[10];

                        sprintf(str_idx, "%d", i);
                        xmlNewProp(sub_xnode, xStrIdx, (const xmlChar *)str_idx);
                }
#endif

                RPT(RPT_INF, "stru2node[%d]:", i);
                param2xml(mem, sub_xnode, (struct pdesc *)(adesc->pdesc));
                xmlAddChild(xnode, sub_xnode);
        }
        return 0;
}

static int node2sint(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        xmlChar *idx;
        xmlChar *ctnt;
        char *p;
        char *endp;

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= count) {
                RPT(RPT_INF, "node2sint: idx(%d) >= count(%d), ignore", pdesc->index, count);
                return -1;
        }
        mem += (pdesc->index * size);

        /* get sint one by one */
        ctnt = xmlNodeGetContent(xnode);
        if(!ctnt) {
                RPT(RPT_ERR, "node2sint: no content");
                return -1;
        }
        p = (char *)ctnt;
        switch(size) {
                case sizeof(int8_t):
                        {
                                int8_t data;
                                int8_t *cur_mem = (int8_t *)mem;

                                while(1) {
                                        data = (int8_t)strtoimax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2sint[%d]: %" PRId8, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                case sizeof(int16_t):
                        {
                                int16_t data;
                                int16_t *cur_mem = (int16_t *)mem;

                                while(1) {
                                        data = (int16_t)strtoimax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2sint[%d]: %" PRId16, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                case sizeof(int32_t):
                        {
                                int32_t data;
                                int32_t *cur_mem = (int32_t *)mem;

                                while(1) {
                                        data = (int32_t)strtoimax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2sint[%d]: %" PRId32, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                case sizeof(int64_t):
                        {
                                int64_t data;
                                int64_t *cur_mem = (int64_t *)mem;

                                while(1) {
                                        data = (int64_t)strtoimax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2sint[%d]: %" PRId64, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                default:
                        RPT(RPT_INF, "node2sint: bad data size(%zd)", size);
                        break;
        }
        xmlFree(ctnt);
        return 0;
}

static int node2uint(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        xmlChar *idx;
        xmlChar *ctnt;
        char *p;
        char *endp;

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= count) {
                RPT(RPT_INF, "node2uint: idx(%d) >= count(%d), ignore", pdesc->index, count);
                return -1;
        }
        mem += (pdesc->index * size);

        /* get uint one by one */
        ctnt = xmlNodeGetContent(xnode);
        if(!ctnt) {
                RPT(RPT_ERR, "node2uint: no content");
                return -1;
        }
        p = (char *)ctnt;
        switch(size) {
                case sizeof(uint8_t):
                        {
                                uint8_t data;
                                uint8_t *cur_mem = (uint8_t *)mem;

                                while(1) {
                                        data = (uint8_t)strtoumax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2uint[%d]: 0x%" PRIX8, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                case sizeof(uint16_t):
                        {
                                uint16_t data;
                                uint16_t *cur_mem = (uint16_t *)mem;

                                while(1) {
                                        data = (uint16_t)strtoumax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2uint[%d]: 0x%" PRIX16, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                case sizeof(uint32_t):
                        {
                                uint32_t data;
                                uint32_t *cur_mem = (uint32_t *)mem;

                                while(1) {
                                        data = (uint32_t)strtoumax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2uint[%d]: 0x%" PRIX32, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                case sizeof(uint64_t):
                        {
                                uint64_t data;
                                uint64_t *cur_mem = (uint64_t *)mem;

                                while(1) {
                                        data = (uint64_t)strtoumax(p, &endp, 0);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2uint[%d]: 0x%" PRIX64, pdesc->index, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                }
                        }
                        break;
                default:
                        RPT(RPT_INF, "node2uint: bad data size(%d)", size);
                        break;
        }
        xmlFree(ctnt);
        return 0;
}

static int node2flot(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        xmlChar *idx;
        xmlChar *ctnt;
        char *p;
        char *endp;

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= count) {
                RPT(RPT_INF, "node2flot: idx(%d) >= count(%d), ignore", pdesc->index, count);
                return -1;
        }
        mem += (pdesc->index * size);

        /* get float one by one */
        ctnt = xmlNodeGetContent(xnode);
        if(!ctnt) {
                RPT(RPT_ERR, "node2flot: no content");
                return -1;
        }
        p = (char *)ctnt;
        switch(size) {
                case sizeof(float):
                        {
                                float data;
                                float *cur_mem = (float *)mem;

                                while(1) {
                                        data = (float)strtod(p, &endp); /* strtof() is seldom */
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2flot[%d]: %.*e", pdesc->index, FLT_DIG + 1, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                };
                        }
                        break;
                case sizeof(double):
                        {
                                double data;
                                double *cur_mem = (double *)mem;

                                while(1) {
                                        data = strtod(p, &endp);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2flot[%d]: %.*e", pdesc->index, DBL_DIG + 1, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                };
                        }
                        break;
#ifdef SUPPORT_LONG_DOUBLE
                case sizeof(long double):
                        {
                                long double data;
                                long double *cur_mem = (long double *)mem;

                                while(1) {
                                        data = strtold(p, &endp);
                                        if(p == endp) {break;}
                                        RPT(RPT_INF, "node2flot[%d]: %.*Le", pdesc->index, LDBL_DIG + 1, data);
                                        *cur_mem++ = data;
                                        pdesc->index++;
                                        p = endp;
                                };
                        }
                        break;
#endif
                default:
                        RPT(RPT_INF, "node2flot: bad data size(%zd)", size);
                        break;
        }
        xmlFree(ctnt);
        return 0;
}

static int node2stru(void *mem, xmlNode *xnode, struct pdesc *pdesc, int count, int size)
{
        xmlChar *idx;
        struct adesc *adesc;

        adesc = (struct adesc *)(pdesc->aux);
        if(!adesc) {
                RPT(RPT_ERR, "node2stru: bad adesc");
                return -1;
        }

        /* adjust pdesc->index, calc mem */
        idx = xmlGetProp(xnode, xStrIdx);
        if(idx) {
                pdesc->index = atoi((char *)idx);
                xmlFree(idx);
        }
        if(pdesc->index >= count) {
                RPT(RPT_INF, "node2stru: idx(%d) >= count(%d), ignore", pdesc->index, count);
                return -1;
        }
        mem += (pdesc->index * size);

        /* get struct */
        RPT(RPT_INF, "node2stru[%d]:", pdesc->index);
        pdesc->index++;
        xml2param(mem, xnode, (struct pdesc *)(adesc->pdesc));
        return 0;
}

#if 0
/* low level functions */

static int ftostr(double data, char *str)
{
        int i, j, k;
        long temp, tempoten;
        char intpart[20], dotpart[20];

        if(data < 0) {
                str[0] = '-';
                data = -data;
        }
        else {
                str[0] = '+';
        }

        temp = (long)data;
        i = 0;
        tempoten = temp / 10;
        while(tempoten != 0) {
                intpart[i] = temp - 10 * tempoten + 48;
                temp = tempoten;
                tempoten = temp / 10;
                i++;
        }
        intpart[i] = temp + 48;


        data = data - (long)data;
        for(j = 0; j < 12; j++) {
                dotpart[j] = (int)(data * 10) + 48;
                data = data * 10.0;
                data = data - (long)data;
        }

        for(k = 1; k <= i + 1; k++) str[k] = intpart[i + 1 - k];
        str[i + 2] = '.';
        for(k = i + 3; k < i + j + 3; k++) str[k] = dotpart[k - i - 3];
        str[i + j + 3] = 0x0D;

        return i+j+4;
}

static intmax_t strtoimax(const char *nptr, char **endptr, int base)
{
        const char *s;
        char c;
        int neg;
        intmax_t acc;
        int any;
        uintmax_t cutoff;
        int cutlim;

        acc = 0;
        any = 0;

        /* remove white space at head */
        s = nptr;
        do {
                c = *s++;
                if(DATA_SPACE == c) {c = ' ';}
        }while(isspace((unsigned char)c));

        /* deal with '+' or '-' */
        if(c == '-') {
                neg = 1;
                c = *s++;
        }
        else{
                neg = 0;
                if(c == '+') {c = *s++;}
        }

        /* base */
        if((base == 0 || base == 16) &&
           c == '0' &&
           (*s == 'x' || *s == 'X') &&
           ((s[1] >= '0' && s[1] <= '9') ||
            (s[1] >= 'A' && s[1] <= 'F') ||
            (s[1] >= 'a' && s[1] <= 'f'))) {
                c = s[1];
                s += 2;
                base = 16;
        }
        if(base == 0) {
                base = ((c == '0') ? 8 : 10);
        }
        if(base < 2 || base > 36) {
                goto noconv; /* bad base */
        }

        /* use cutoff and cutlim to judge overflow */
        cutoff = (neg ? uintmax_imin : uintmax_imax);
        cutlim = cutoff % base;
        cutoff /= base;

        for(; ; c = *s++) {
                /* get one number */
                if     (c >= '0' && c <= '9') {c -= '0';}
                else if(c >= 'A' && c <= 'Z') {c -= ('A' - 10);}
                else if(c >= 'a' && c <= 'z') {c -= ('a' - 10);}
                else                          {break;}

                if(c >= base) {
                        break; /* bad number, stop */
                }

                if(any < 0 ||
                   acc > cutoff ||
                   (acc == cutoff && c > cutlim)) {
                        any = -1; /* overflow */
                }
                else {
                        any = 1;
                        acc *= base;
                        acc += c;
                }
        }

        if(any < 0) {
                /* overflow */
                acc = intmax_max;
                acc += (neg ? 1 : 0); /* intmax_min */
        }
        else if(!any) {
noconv:
                any = 0; /* invalid data */
        }
        else if(neg) {
                acc = -acc;
        }

        if (endptr != NULL) {
                *endptr = (char *)(any ? s - 1 : nptr);
        }

        return (acc);
}

static uintmax_t strtoumax(const char *nptr, char **endptr, int base)
{
        const char *s;
        char c;
        uintmax_t acc;
        int any;
        uintmax_t cutoff;
        int cutlim;

        acc = 0;
        any = 0;

        /* remove white space at head */
        s = nptr;
        do {
                c = *s++;
                if(DATA_SPACE == c) {c = ' ';}
        }while(isspace((unsigned char)c));

        /* deal with '+' or '-' */
        if(c == '-') {
                goto noconv; /* bad base */
        }
        else if(c == '+') {
                c = *s++;
        }

        /* base */
        if((base == 0 || base == 16) &&
           c == '0' &&
           (*s == 'x' || *s == 'X') &&
           ((s[1] >= '0' && s[1] <= '9') ||
            (s[1] >= 'A' && s[1] <= 'F') ||
            (s[1] >= 'a' && s[1] <= 'f'))) {
                c = s[1];
                s += 2;
                base = 16;
        }
        if(base == 0) {
                base = ((c == '0') ? 8 : 10);
        }
        if(base < 2 || base > 36) {
                goto noconv; /* bad base */
        }

        /* use cutoff and cutlim to judge overflow */
        cutoff = uintmax_umax;
        cutlim = cutoff % base;
        cutoff /= base;

        for(; ; c = *s++) {
                /* get one number */
                if     (c >= '0' && c <= '9') {c -= '0';}
                else if(c >= 'A' && c <= 'Z') {c -= ('A' - 10);}
                else if(c >= 'a' && c <= 'z') {c -= ('a' - 10);}
                else                          {break;}

                if(c >= base) {
                        break; /* bad number, stop */
                }

                if(any < 0 ||
                   acc > cutoff ||
                   (acc == cutoff && c > cutlim)) {
                        any = -1; /* overflow */
                }
                else {
                        any = 1;
                        acc *= base;
                        acc += c;
                }
        }

        if(any < 0) {
                acc = uintmax_umax; /* overflow */
        }
        else if(!any) {
noconv:
                any = 0; /* invalid data */
        }

        if (endptr != NULL) {
                *endptr = (char *)(any ? s - 1 : nptr);
        }

        return (acc);
}
#endif
