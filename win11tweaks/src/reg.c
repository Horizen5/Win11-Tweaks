/* ============================================================================
 *  reg.c  --  注册表读写封装
 *  ----------------------------------------------------------------------------
 *  直接走 Win32 API（RegSetValueExW / RegQueryValueExW），不经过 reg.exe。
 *  这一点很关键：Windows 的 UCPD 驱动（UserChoice Protection Driver）只按
 *  “调用进程的文件名”做黑名单匹配（reg.exe / powershell.exe / cmd.exe …），
 *  本程序文件名不在名单里，因此受保护的值（TaskbarDa、SearchboxTaskbarMode
 *  等）可以直接写入，不需要“复制 reg.exe 改名再执行”那套绕过手段。
 * ========================================================================== */
#include "wt.h"

static HKEY hiveRoot(BYTE hive)
{
    return (hive == HIVE_LM) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

/* ---------------------------------------------------------------- 打开/创建 */
static HKEY openKey(BYTE hive, const WCHAR *sub, REGSAM access, BOOL create)
{
    HKEY k = NULL;
    if (create) {
        if (RegCreateKeyExW(hiveRoot(hive), sub, 0, NULL, 0, access, NULL, &k, NULL) != ERROR_SUCCESS)
            return NULL;
    } else {
        if (RegOpenKeyExW(hiveRoot(hive), sub, 0, access, &k) != ERROR_SUCCESS)
            return NULL;
    }
    return k;
}

/* ---------------------------------------------------------------- 写 */

BOOL RegWriteVal(BYTE hive, const WCHAR *sub, const WCHAR *name, DWORD type,
                 const void *d, DWORD n)
{
    HKEY k = openKey(hive, sub, KEY_SET_VALUE, TRUE);
    LONG r;
    if (!k) return FALSE;
    r = RegSetValueExW(k, name, 0, type, (const BYTE *)d, n);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

BOOL RegWriteDword(BYTE hive, const WCHAR *sub, const WCHAR *name, DWORD v)
{
    return RegWriteVal(hive, sub, name, REG_DWORD, &v, sizeof(v));
}

BOOL RegWriteSz(BYTE hive, const WCHAR *sub, const WCHAR *name, const WCHAR *v)
{
    size_t cb = (wcslen(v) + 1) * sizeof(WCHAR);
    return RegWriteVal(hive, sub, name, REG_SZ, v, (DWORD)cb);
}

BOOL RegWriteBin(BYTE hive, const WCHAR *sub, const WCHAR *name, const BYTE *d, DWORD n)
{
    return RegWriteVal(hive, sub, name, REG_BINARY, d, n);
}

BOOL RegDelValue(BYTE hive, const WCHAR *sub, const WCHAR *name)
{
    HKEY k = openKey(hive, sub, KEY_SET_VALUE, FALSE);
    LONG r;
    if (!k) return TRUE;                    /* 键都不存在 = 已经等效于删除 */
    r = RegDeleteValueW(k, name);
    RegCloseKey(k);
    return (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
}

/* ---------------------------------------------------------------- 读 */

BOOL RegReadOp(const RegOp *op, ValState *st)
{
    HKEY k;
    DWORD type = 0, cb = 0;
    LONG r;
    BYTE *buf;

    memset(st, 0, sizeof(*st));
    st->opIdx = -1;
    st->exists = FALSE;
    st->type = op->type;

    k = openKey(op->hive, op->subkey, KEY_QUERY_VALUE, FALSE);
    if (!k) return TRUE;                    /* 键不存在 -> 值即不存在 */

    r = RegQueryValueExW(k, op->name, NULL, &type, NULL, &cb);
    if (r != ERROR_SUCCESS) { RegCloseKey(k); return TRUE; }   /* ERROR_FILE_NOT_FOUND 等 */

    buf = (BYTE *)wt_alloc((size_t)cb + 4);
    r = RegQueryValueExW(k, op->name, NULL, &type, buf, &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS) { free(buf); return TRUE; }

    st->exists = TRUE;
    st->type = type;

    switch (type) {
    case REG_DWORD:
    case REG_DWORD_BIG_ENDIAN:
        if (cb >= 4) st->dw = *(const DWORD *)buf;
        break;
    case REG_SZ:
    case REG_EXPAND_SZ:
        st->sz = wt_strndup((const WCHAR *)buf, cb / sizeof(WCHAR));
        /* 去掉可能的结尾 NUL */
        if (st->sz && st->sz[0]) {
            size_t l = wcslen(st->sz);
            (void)l;
        }
        if (st->sz && cb >= sizeof(WCHAR) && st->sz[wcslen(st->sz)] == 0) { /* ok */ }
        break;
    case REG_BINARY:
    default:
        st->bin = (BYTE *)wt_alloc((size_t)cb + 1);
        memcpy(st->bin, buf, cb);
        st->binLen = cb;
        break;
    }
    free(buf);
    return TRUE;
}
