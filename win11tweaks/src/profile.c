/* ============================================================================
 *  profile.c  --  配置快照的导出 / 导入 / 差异比对
 *  ----------------------------------------------------------------------------
 *  文件格式：UTF-8(带 BOM) 的 JSON，人可读、可手改、可 diff。
 *  JSON 读写都是自己实现的极简版，不引入任何第三方库（保证体积与零依赖）。
 * ========================================================================== */
#include "wt.h"

/* ============================================================ JSON 写 */

static void jEsc(StrBuf *sb, const WCHAR *s)
{
    sbAddCh(sb, L'"');
    for (; s && *s; s++) {
        switch (*s) {
        case L'"':  sbAdd(sb, L"\\\""); break;
        case L'\\': sbAdd(sb, L"\\\\"); break;
        case L'\n': sbAdd(sb, L"\\n");  break;
        case L'\r': sbAdd(sb, L"\\r");  break;
        case L'\t': sbAdd(sb, L"\\t");  break;
        default:
            if (*s < 0x20) sbAddF(sb, L"\\u%04X", (unsigned)*s);
            else sbAddCh(sb, *s);
        }
    }
    sbAddCh(sb, L'"');
}

static const WCHAR *typeName(DWORD t)
{
    switch (t) {
    case REG_SZ:        return L"REG_SZ";
    case REG_EXPAND_SZ: return L"REG_EXPAND_SZ";
    case REG_BINARY:    return L"REG_BINARY";
    default:            return L"REG_DWORD";
    }
}

static DWORD typeFromName(const WCHAR *s)
{
    if (wt_ieq(s, L"REG_SZ"))        return REG_SZ;
    if (wt_ieq(s, L"REG_EXPAND_SZ")) return REG_EXPAND_SZ;
    if (wt_ieq(s, L"REG_BINARY"))    return REG_BINARY;
    return REG_DWORD;
}

static void jElem(StrBuf *sb, const ValState *st, const RegOp *op, int last)
{
    const TweakDef *td = &g_tweaks[op->tweak];
    sbAdd(sb, L"    {\r\n");
    sbAdd(sb, L"      \"id\": ");     jEsc(sb, td->id);           sbAdd(sb, L",\r\n");
    sbAdd(sb, L"      \"label\": ");  jEsc(sb, td->label);        sbAdd(sb, L",\r\n");
    sbAdd(sb, L"      \"hive\": ");   jEsc(sb, op->hive == HIVE_LM ? L"HKLM" : L"HKCU"); sbAdd(sb, L",\r\n");
    sbAdd(sb, L"      \"key\": ");    jEsc(sb, op->subkey);       sbAdd(sb, L",\r\n");
    sbAdd(sb, L"      \"name\": ");   jEsc(sb, op->name ? op->name : L""); sbAdd(sb, L",\r\n");
    sbAdd(sb, L"      \"type\": ");
    jEsc(sb, typeName(st->exists ? st->type : op->type));
    sbAdd(sb, L",\r\n");
    sbAddF(sb, L"      \"exists\": %ls,\r\n", st->exists ? L"true" : L"false");
    sbAdd(sb, L"      \"data\": ");
    if (!st->exists) {
        sbAdd(sb, L"null");
    } else if (st->type == REG_DWORD || st->type == REG_DWORD_BIG_ENDIAN) {
        sbAddF(sb, L"%lu", (unsigned long)st->dw);
    } else if (st->type == REG_BINARY) {
        StrBuf h; sbInit(&h);
        wt_hexenc(st->bin, st->binLen, &h);
        jEsc(sb, h.p ? h.p : L"");
        sbFree(&h);
    } else {
        jEsc(sb, st->sz ? st->sz : L"");
    }
    sbAdd(sb, L"\r\n    }");
    if (!last) sbAddCh(sb, L',');
    sbAdd(sb, L"\r\n");
}

BOOL ProfileSaveJson(const Profile *pf, const WCHAR *path)
{
    StrBuf sb;
    int i;
    char *utf8;
    int n8 = 0;
    BYTE *out;
    BOOL ok;

    sbInit(&sb);
    sbAdd(&sb, L"{\r\n");
    sbAdd(&sb, L"  \"format\": \"Win11Tweaks.Profile\",\r\n");
    sbAdd(&sb, L"  \"version\": 1,\r\n");
    sbAdd(&sb, L"  \"note\": \"本文件由 Win11Tweaks 导出。导入时只写差异项，且会先自动备份当前值。\",\r\n");
    sbAdd(&sb, L"  \"exported\": "); jEsc(&sb, pf->exported ? pf->exported : L""); sbAdd(&sb, L",\r\n");
    sbAdd(&sb, L"  \"machine\": ");  jEsc(&sb, pf->machine ? pf->machine : L"");   sbAdd(&sb, L",\r\n");
    sbAdd(&sb, L"  \"user\": ");     jEsc(&sb, pf->user ? pf->user : L"");         sbAdd(&sb, L",\r\n");
    sbAdd(&sb, L"  \"os\": ");       jEsc(&sb, pf->os ? pf->os : L"");             sbAdd(&sb, L",\r\n");
    sbAddF(&sb, L"  \"count\": %d,\r\n", pf->count);
    sbAdd(&sb, L"  \"values\": [\r\n");
    for (i = 0; i < pf->count; i++)
        jElem(&sb, &pf->vals[i], &g_ops[i], i == pf->count - 1);
    sbAdd(&sb, L"  ]\r\n}\r\n");

    utf8 = wt_utf16_to_utf8(sb.p, (int)sb.len, &n8);
    out = (BYTE *)wt_alloc((size_t)n8 + 3);
    out[0] = 0xEF; out[1] = 0xBB; out[2] = 0xBF;
    if (n8) memcpy(out + 3, utf8, (size_t)n8);
    ok = wt_writeAll(path, out, (DWORD)n8 + 3);
    free(out);
    free(utf8);
    sbFree(&sb);
    return ok;
}

/* ============================================================ JSON 读（极简） */

enum { JV_NULL, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ };

typedef struct JV {
    int     type;
    int     b;
    double  num;
    WCHAR  *str;
    struct JV **items;  int n;            /* 数组 */
    WCHAR **keys;  struct JV **vals; int nk;  /* 对象 */
} JV;

static void jFree(JV *v)
{
    int i;
    if (!v) return;
    free(v->str);
    for (i = 0; i < v->n; i++) jFree(v->items[i]);
    free(v->items);
    for (i = 0; i < v->nk; i++) { free(v->keys[i]); jFree(v->vals[i]); }
    free(v->keys); free(v->vals);
    free(v);
}

typedef struct { const WCHAR *p; } JP;

static void jws(JP *j) { while (*j->p == L' ' || *j->p == L'\t' || *j->p == L'\r' || *j->p == L'\n') j->p++; }

static int jHex4(JP *j, WCHAR *out)
{
    int i, v = 0;
    for (i = 0; i < 4; i++) {
        WCHAR c = j->p[i];
        int d;
        if (c >= L'0' && c <= L'9') d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
        else return 0;
        v = v * 16 + d;
    }
    j->p += 4;
    *out = (WCHAR)v;
    return 1;
}

static WCHAR *jStr(JP *j)
{
    StrBuf sb;
    if (*j->p != L'"') return NULL;
    j->p++;
    sbInit(&sb);
    while (*j->p && *j->p != L'"') {
        if (*j->p == L'\\') {
            j->p++;
            switch (*j->p) {
            case L'"':  sbAddCh(&sb, L'"');  j->p++; break;
            case L'\\': sbAddCh(&sb, L'\\'); j->p++; break;
            case L'/':  sbAddCh(&sb, L'/');  j->p++; break;
            case L'b':  sbAddCh(&sb, L'\b'); j->p++; break;
            case L'f':  sbAddCh(&sb, L'\f'); j->p++; break;
            case L'n':  sbAddCh(&sb, L'\n'); j->p++; break;
            case L'r':  sbAddCh(&sb, L'\r'); j->p++; break;
            case L't':  sbAddCh(&sb, L'\t'); j->p++; break;
            case L'u': {
                WCHAR w1;
                j->p++;
                if (!jHex4(j, &w1)) { sbAddCh(&sb, L'?'); break; }
                if (w1 >= 0xD800 && w1 <= 0xDBFF && j->p[0] == L'\\' && j->p[1] == L'u') {
                    WCHAR w2; const WCHAR *save = j->p;
                    j->p += 2;
                    if (jHex4(j, &w2) && w2 >= 0xDC00 && w2 <= 0xDFFF) {
                        sbAddCh(&sb, w1); sbAddCh(&sb, w2);
                    } else { j->p = save; sbAddCh(&sb, w1); }
                } else {
                    sbAddCh(&sb, w1);
                }
                break;
            }
            default: sbAddCh(&sb, *j->p); if (*j->p) j->p++; break;
            }
        } else {
            sbAddCh(&sb, *j->p++);
        }
    }
    if (*j->p == L'"') j->p++;
    return sbDetach(&sb);
}

static JV *jVal(JP *j);

static JV *jArr(JP *j)
{
    JV *v = (JV *)wt_alloc(sizeof(JV));
    memset(v, 0, sizeof(*v));
    v->type = JV_ARR;
    j->p++;                     /* [ */
    jws(j);
    if (*j->p == L']') { j->p++; return v; }
    for (;;) {
        JV *e = jVal(j);
        if (!e) break;
        v->items = (JV **)wt_realloc(v->items, sizeof(JV *) * (size_t)(v->n + 1));
        v->items[v->n++] = e;
        jws(j);
        if (*j->p == L',') { j->p++; jws(j); continue; }
        if (*j->p == L']') { j->p++; break; }
        break;
    }
    return v;
}

static JV *jObj(JP *j)
{
    JV *v = (JV *)wt_alloc(sizeof(JV));
    memset(v, 0, sizeof(*v));
    v->type = JV_OBJ;
    j->p++;                     /* { */
    jws(j);
    if (*j->p == L'}') { j->p++; return v; }
    for (;;) {
        WCHAR *k;
        JV *e;
        jws(j);
        k = jStr(j);
        if (!k) break;
        jws(j);
        if (*j->p != L':') { free(k); break; }
        j->p++;
        e = jVal(j);
        v->keys = (WCHAR **)wt_realloc(v->keys, sizeof(WCHAR *) * (size_t)(v->nk + 1));
        v->vals = (JV **)wt_realloc(v->vals, sizeof(JV *) * (size_t)(v->nk + 1));
        v->keys[v->nk] = k;
        v->vals[v->nk] = e;
        v->nk++;
        jws(j);
        if (*j->p == L',') { j->p++; continue; }
        if (*j->p == L'}') { j->p++; break; }
        break;
    }
    return v;
}

static JV *jVal(JP *j)
{
    jws(j);
    switch (*j->p) {
    case L'{': return jObj(j);
    case L'[': return jArr(j);
    case L'"': {
        JV *v = (JV *)wt_alloc(sizeof(JV));
        memset(v, 0, sizeof(*v));
        v->type = JV_STR;
        v->str = jStr(j);
        return v;
    }
    case L't': if (wt_ieqn(j->p, L"true", 4))  { JV *v = (JV *)wt_alloc(sizeof(JV)); memset(v, 0, sizeof(*v)); v->type = JV_BOOL; v->b = 1; j->p += 4; return v; } break;
    case L'f': if (wt_ieqn(j->p, L"false", 5)) { JV *v = (JV *)wt_alloc(sizeof(JV)); memset(v, 0, sizeof(*v)); v->type = JV_BOOL; v->b = 0; j->p += 5; return v; } break;
    case L'n': if (wt_ieqn(j->p, L"null", 4))  { JV *v = (JV *)wt_alloc(sizeof(JV)); memset(v, 0, sizeof(*v)); v->type = JV_NULL; j->p += 4; return v; } break;
    default: break;
    }
    {
        WCHAR *end = NULL;
        double d = wcstod(j->p, &end);
        if (end && end != j->p) {
            JV *v = (JV *)wt_alloc(sizeof(JV));
            memset(v, 0, sizeof(*v));
            v->type = JV_NUM;
            v->num = d;
            j->p = end;
            return v;
        }
    }
    return NULL;
}

static JV *jGet(const JV *o, const WCHAR *key)
{
    int i;
    if (!o || o->type != JV_OBJ) return NULL;
    for (i = 0; i < o->nk; i++) if (wt_ieq(o->keys[i], key)) return o->vals[i];
    return NULL;
}

static const WCHAR *jGetStr(const JV *o, const WCHAR *key)
{
    JV *v = jGet(o, key);
    return (v && v->type == JV_STR) ? v->str : NULL;
}

/* ============================================================ 载入 */

/* 按 (hive,key,name) 在本工具的值表里定位；找不到返回 -1（版本更新时容错） */
static int findOp(const WCHAR *hive, const WCHAR *key, const WCHAR *name)
{
    int i;
    for (i = 0; i < g_opCount; i++) {
        const RegOp *op = &g_ops[i];
        const WCHAR *hn = (op->hive == HIVE_LM) ? L"HKLM" : L"HKCU";
        if (!wt_ieq(hive, hn)) continue;
        if (!wt_ieq(op->subkey, key)) continue;
        if (op->name == NULL) { if (!name || !*name) return i; }
        else if (name && wt_ieq(op->name, name)) return i;
    }
    return -1;
}

Profile *ProfileLoadJson(const WCHAR *path, WCHAR *err, int errCch)
{
    BYTE *raw = NULL;
    DWORD n = 0;
    WCHAR *text = NULL;
    JP j;
    JV *root, *vals;
    Profile *pf;
    int i, found = 0;

    if (err && errCch) err[0] = 0;
    if (!wt_readAll(path, &raw, &n)) {
        if (err) wt_strlcpy(err, L"无法读取文件", (size_t)errCch);
        return NULL;
    }
    wt_utf8_to_utf16((const char *)raw, (int)n, &text);   /* 内含 BOM 处理 */
    free(raw);
    if (!text) { if (err) wt_strlcpy(err, L"文件编码无法识别", (size_t)errCch); return NULL; }

    j.p = text;
    root = jVal(&j);
    if (!root || root->type != JV_OBJ) {
        if (err) wt_strlcpy(err, L"不是合法的 JSON 对象", (size_t)errCch);
        jFree(root); free(text);
        return NULL;
    }
    if (!wt_ieq(jGetStr(root, L"format") ? jGetStr(root, L"format") : L"", L"Win11Tweaks.Profile")) {
        if (err) wt_strlcpy(err, L"不是本工具导出的配置文件（format 字段不匹配）", (size_t)errCch);
        jFree(root); free(text);
        return NULL;
    }

    /* 必须走 ProfileNew()：它会把 vals 全量清零。
       （直接 wt_alloc 再逐项赋值曾导致 free() 野指针 -> 堆损坏） */
    pf = ProfileNew();
    for (i = 0; i < g_opCount; i++) pf->vals[i].opIdx = i;

    {
        const WCHAR *s;
        s = jGetStr(root, L"machine");  pf->machine  = wt_strdup(s ? s : L"");
        s = jGetStr(root, L"user");     pf->user     = wt_strdup(s ? s : L"");
        s = jGetStr(root, L"os");       pf->os       = wt_strdup(s ? s : L"");
        s = jGetStr(root, L"exported"); pf->exported = wt_strdup(s ? s : L"");
    }

    vals = jGet(root, L"values");
    if (!vals || vals->type != JV_ARR) {
        if (err) wt_strlcpy(err, L"缺少 values 数组", (size_t)errCch);
        ProfileFree(pf); jFree(root); free(text);
        return NULL;
    }

    for (i = 0; i < vals->n; i++) {
        JV *e = vals->items[i];
        const WCHAR *hive, *key, *name;
        JV *ex, *dt;
        int idx;
        if (!e || e->type != JV_OBJ) continue;
        hive = jGetStr(e, L"hive");
        key  = jGetStr(e, L"key");
        name = jGetStr(e, L"name");
        if (!hive || !key) continue;
        idx = findOp(hive, key, name);
        if (idx < 0) continue;               /* 本版本不认识的值：忽略 */
        ex = jGet(e, L"exists");
        dt = jGet(e, L"data");
        pf->vals[idx].exists = (ex && ex->type == JV_BOOL) ? (ex->b ? TRUE : FALSE) : FALSE;
        if (!pf->vals[idx].exists) { found++; continue; }
        pf->vals[idx].type = typeFromName(jGetStr(e, L"type"));
        if (pf->vals[idx].type == REG_DWORD) {
            if (dt && dt->type == JV_NUM) pf->vals[idx].dw = (DWORD)dt->num;
        } else if (pf->vals[idx].type == REG_BINARY) {
            if (dt && dt->type == JV_STR) wt_hexdec(dt->str, &pf->vals[idx].bin, &pf->vals[idx].binLen);
        } else {
            pf->vals[idx].sz = wt_strdup((dt && dt->type == JV_STR) ? dt->str : L"");
        }
        found++;
    }

    jFree(root);
    free(text);
    if (found == 0 && err) wt_strlcpy(err, L"配置文件中没有任何本版本认识的值", (size_t)errCch);
    return pf;
}

/* ============================================================ 差异 */

static int valSame(const ValState *a, const ValState *b, DWORD wantType)
{
    if (a->exists != b->exists) return 0;
    if (!a->exists) return 1;
    if (a->type != b->type) return 0;
    switch (a->type) {
    case REG_DWORD:
        return a->dw == b->dw;
    case REG_BINARY:
        if (a->binLen != b->binLen) return 0;
        if (a->binLen == 0) return 1;
        return memcmp(a->bin, b->bin, a->binLen) == 0;
    default:
        (void)wantType;
        return a->sz && b->sz && wt_ieq(a->sz, b->sz);
    }
}

int ProfileDiff(const Profile *target, const Profile *cur, DiffItem **out)
{
    int i;
    DiffItem *d = (DiffItem *)wt_alloc(sizeof(DiffItem) * (size_t)g_opCount);
    for (i = 0; i < g_opCount; i++) {
        const ValState *t = &target->vals[i];
        const ValState *c = &cur->vals[i];
        d[i].opIdx = i;
        if (t->exists == c->exists && valSame(t, c, g_ops[i].type)) d[i].kind = DIFF_SAME;
        else if (!t->exists && c->exists)                         d[i].kind = DIFF_DEL;
        else if (t->exists && !c->exists)                         d[i].kind = DIFF_ADD;
        else                                                      d[i].kind = DIFF_CHG;
    }
    *out = d;
    return g_opCount;
}

/* 把一个值格式化成便于阅读的短文本 */
void ValToText(const ValState *st, WCHAR *buf, int cch)
{
    if (!st->exists) { wt_strlcpy(buf, L"(不存在)", (size_t)cch); return; }
    if (st->type == REG_DWORD) {
        if (st->dw == 0)      wt_strlcpy(buf, L"0", (size_t)cch);
        else if (st->dw == 1) wt_strlcpy(buf, L"1", (size_t)cch);
        else _snwprintf_s(buf, (size_t)cch, _TRUNCATE, L"%lu", (unsigned long)st->dw);
    } else if (st->type == REG_BINARY) {
        int i, p = 0;
        p += _snwprintf_s(buf + p, (size_t)(cch - p), _TRUNCATE, L"[%lu 字节] ", (unsigned long)st->binLen);
        for (i = 0; i < (int)st->binLen && p < cch - 4 && i < 8; i++)
            p += _snwprintf_s(buf + p, (size_t)(cch - p), _TRUNCATE, L"%02X", st->bin[i]);
    } else {
        const WCHAR *s = st->sz ? st->sz : L"";
        if (wcslen(s) > 60) {
            wcsncpy_s(buf, (size_t)cch, s, 57);
            wt_strlcat(buf, L"...", (size_t)cch);
        } else {
            wt_strlcpy(buf, s, (size_t)cch);
        }
    }
}
