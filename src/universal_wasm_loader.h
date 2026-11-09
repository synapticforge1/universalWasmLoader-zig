/*
 * universal_wasm_loader.h — Universal WASM Loader, C / header-only port.
 *
 * The C sibling of the reference TypeScript/JavaScript loader
 * (`@jrmarcum/universal-wasm-loader`). Loads a `.wasm` *reactor / library*
 * module produced by `wasmtk` (e.g. from TypeScript via `wasic`/`modc`),
 * auto-detects the companion `.wit` file, and applies the Canonical ABI so the
 * caller invokes exports by name with native C values. Built on the
 * **wasmtime C API**. Conforms to the cross-language SPEC v4.0.0
 * (callee-allocated string returns + `cabi_post_<name>`; loud failures: only
 * an absent `.wit` is raw, every WIT import needs a callback, typed calls and
 * callback results are checked, never coerced).
 *
 * Intended consumers: C, C++, and — via the C ABI — Zig, V, and Julia.
 *
 * ---------------------------------------------------------------------------
 * Usage (STB-style single header):
 *
 *   // In exactly ONE translation unit:
 *   #define UWL_IMPLEMENTATION
 *   #include "universal_wasm_loader.h"
 *
 *   // In every other unit, just:
 *   #include "universal_wasm_loader.h"
 *
 * Link against the wasmtime C API (e.g. -lwasmtime). See the Makefile.
 *
 * ---------------------------------------------------------------------------
 * Example:
 *
 *   char *err = NULL;
 *   uwl_module_t *m = uwl_import("math_50.wasm", NULL, 0, &err);
 *   if (!m) { fprintf(stderr, "%s\n", err); uwl_string_free(err); return 1; }
 *
 *   uwl_val_t args[2] = { uwl_i32(3), uwl_i32(4) };
 *   uwl_val_t out;
 *   if (uwl_call(m, "add", args, 2, &out, &err) == 0)
 *       printf("add(3,4) = %d\n", uwl_as_i32(out));   // 7
 *   uwl_val_free(&out);
 *   uwl_free(m);
 *
 * Strings: arguments built with uwl_str(...) are copied into the loader; free
 * them with uwl_val_free when you built an owned value. String results written
 * to `out` are heap-owned — free with uwl_val_free(&out). Error strings from
 * the `err` out-param are heap-owned — free with uwl_string_free(err).
 */
#ifndef UNIVERSAL_WASM_LOADER_H
#define UNIVERSAL_WASM_LOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Value type ─────────────────────────────────────────────────────────── */

/** Discriminant for #uwl_val_t. */
typedef enum {
    UWL_VOID = 0, /**< No value (void return). */
    UWL_I32,      /**< 32-bit signed integer (WIT `s32`). */
    UWL_I64,      /**< 64-bit signed integer (WIT `s64`). */
    UWL_F32,      /**< 32-bit float (WIT `f32`). */
    UWL_F64,      /**< 64-bit float (WIT `f64`). */
    UWL_BOOL,     /**< Boolean (WIT `bool`). */
    UWL_STR,      /**< UTF-8 string (WIT `string`). */
    UWL_ERR       /**< A host callback's failure; `str` is the message. See #uwl_err. */
} uwl_kind_t;

/**
 * A dynamically-typed value crossing the host↔WASM boundary with ABI
 * translation applied.
 *
 * When `kind == UWL_STR` (or #UWL_ERR, whose `str` is the message), `str`
 * points to a heap-allocated, NUL-terminated buffer of `len` bytes that the
 * value owns. Free owned values with #uwl_val_free.
 */
typedef struct uwl_val {
    uwl_kind_t kind;
    union {
        int32_t i32;
        int64_t i64;
        float   f32;
        double  f64;
        int     b;
    } as;
    char  *str; /**< Owned UTF-8 bytes when `kind == UWL_STR` / `UWL_ERR`, else NULL. */
    size_t len; /**< Byte length of `str` when `kind == UWL_STR`. */
} uwl_val_t;

/* Value constructors. */
static inline uwl_val_t uwl_i32(int32_t v)  { uwl_val_t x; x.kind = UWL_I32;  x.as.i32 = v; x.str = NULL; x.len = 0; return x; }
static inline uwl_val_t uwl_i64(int64_t v)  { uwl_val_t x; x.kind = UWL_I64;  x.as.i64 = v; x.str = NULL; x.len = 0; return x; }
static inline uwl_val_t uwl_f32(float v)    { uwl_val_t x; x.kind = UWL_F32;  x.as.f32 = v; x.str = NULL; x.len = 0; return x; }
static inline uwl_val_t uwl_f64(double v)   { uwl_val_t x; x.kind = UWL_F64;  x.as.f64 = v; x.str = NULL; x.len = 0; return x; }
static inline uwl_val_t uwl_bool(int v)     { uwl_val_t x; x.kind = UWL_BOOL; x.as.b   = v ? 1 : 0; x.str = NULL; x.len = 0; return x; }
static inline uwl_val_t uwl_void(void)      { uwl_val_t x; x.kind = UWL_VOID; x.as.i64 = 0; x.str = NULL; x.len = 0; return x; }

/** Construct an owned string value by copying `len` bytes from `s`. */
uwl_val_t uwl_strn(const char *s, size_t len);
/** Construct an owned string value by copying a NUL-terminated `s`. */
uwl_val_t uwl_str(const char *s);
/**
 * Construct a FAILURE for a host callback to return: kind #UWL_ERR, owning a
 * copy of `msg` (NULL gives "(no message)"). The trampoline then traps, for
 * every import, void ones included, so the WASM stops at the import, and the
 * #uwl_call of the export that reached it fails with
 * `host callback <name> (WIT import "<name>") failed: <msg>`. The loader frees
 * the value. Not an argument or result kind: #uwl_call refuses it as one.
 */
uwl_val_t uwl_err(const char *msg);

/* Value accessors (do not transfer ownership). */
static inline int32_t     uwl_as_i32(uwl_val_t v)  { return v.as.i32; }
static inline int64_t     uwl_as_i64(uwl_val_t v)  { return v.as.i64; }
static inline float       uwl_as_f32(uwl_val_t v)  { return v.as.f32; }
static inline double      uwl_as_f64(uwl_val_t v)  { return v.as.f64; }
static inline int         uwl_as_bool(uwl_val_t v) { return v.as.b; }
static inline const char *uwl_as_str(uwl_val_t v)  { return v.str; }

/** Free the heap buffer owned by a string value and reset it to void. */
void uwl_val_free(uwl_val_t *v);
/** Free a heap string returned via an `err` out-parameter. */
void uwl_string_free(char *s);

/* ── Host import callbacks ──────────────────────────────────────────────── */

/**
 * A host function the WASM module imports.
 *
 * Receives decoded arguments (`args`/`nargs`) and returns a single value.
 * String arguments are decoded from WASM memory and owned by the loader for
 * the duration of the call (do not free them). The returned value is encoded
 * back per the import's WIT result type, and must BE that type's kind (an
 * `s32` result needs #uwl_i32, a `bool` result #uwl_bool, …): any other kind,
 * #uwl_void included, is never coerced — it traps, and the #uwl_call of the
 * export that called the import fails naming the callback, the import, the
 * result type and the value. An import with no result ignores the value (the
 * loader frees an owned string either way).
 *
 * To FAIL, return #uwl_err("why"): for any import, void ones included, the
 * WASM stops there and the export's #uwl_call fails with that message.
 */
typedef uwl_val_t (*uwl_host_fn_t)(const uwl_val_t *args, size_t nargs, void *userdata);

/**
 * Binds a camelCase WIT import name to a host callback.
 *
 * `name` is the camelCase form of the WIT import (e.g. WIT `env-mul` → `"envMul"`).
 */
typedef struct uwl_host_callback {
    const char    *name;
    uwl_host_fn_t  fn;
    void          *userdata;
} uwl_host_callback_t;

/* ── Module loading & calling ───────────────────────────────────────────── */

/** An opaque, loaded and ABI-translated WASM module. */
typedef struct uwl_module uwl_module_t;

/**
 * Load a `.wasm` module from a file path and return a handle.
 *
 * The companion `.wit` is auto-detected (the trailing `.wasm` is replaced with
 * `.wit`) and read BEFORE the `.wasm`. If found, it must parse completely (the
 * SPEC §2 grammar; types s32 s64 f32 f64 bool string only), and the Canonical
 * ABI is applied: exports are callable by their camelCase WIT names, and every
 * WIT export must exist in the module. Only an ABSENT `.wit` (the file does
 * not exist, or the path does not end in `.wasm`) gives raw exports, callable
 * by their WASM names with no translation; any other failure to read it, and
 * any fault in it, fails the load naming the `.wit` and the reason.
 *
 * A `@N` suffix on `wasm_path` pins to a module version: after instantiation
 * the module's exported `version` i32 global must equal `N` or loading fails.
 *
 * `callbacks`/`ncallbacks` provide host functions for the module's WIT imports,
 * keyed by camelCase name. EVERY WIT import needs one (an entry whose `fn` is
 * NULL counts as none): a missing one fails the load, naming all of them, and
 * so does a WIT import with a `string` result (not supported). Entries for
 * names the `.wit` does not import are ignored. Pass `NULL`/`0` if the module
 * imports nothing.
 *
 * Returns the module on success, or `NULL` on failure with `*err` set to a
 * heap-allocated message (free with #uwl_string_free). `err` may be `NULL`.
 */
uwl_module_t *uwl_import(const char *wasm_path,
                         const uwl_host_callback_t *callbacks, size_t ncallbacks,
                         char **err);

/**
 * Call an exported function.
 *
 * `name` is the camelCase WIT export name (or the raw WASM name when no `.wit`
 * was found). `args`/`nargs` are the typed arguments; `*out` receives the
 * return value (kind `UWL_VOID` for void exports). String results are
 * heap-owned — free `*out` with #uwl_val_free.
 *
 * With a `.wit`, the count and each argument's kind must match the WIT
 * signature exactly (`s32` ↔ UWL_I32, `bool` ↔ UWL_BOOL, …; a string must not
 * be NULL); nothing is coerced, and a mismatch fails naming the export and
 * the parameter. Raw exports are checked by the engine against the WASM
 * signature instead.
 *
 * Returns 0 on success, or -1 on failure with `*err` set (free with
 * #uwl_string_free). `err` may be `NULL`.
 */
int uwl_call(uwl_module_t *m, const char *name,
             const uwl_val_t *args, size_t nargs,
             uwl_val_t *out, char **err);

/** Free a module and all resources it owns. */
void uwl_free(uwl_module_t *m);

/* ── Convenience call layer ─────────────────────────────────────────────────
 *
 * Typed, variadic wrappers over #uwl_call that read native C arguments and
 * return a native C result — the idiomatic one-liner form:
 *
 *   int    r = uwl_call_i32(m, "add", 3, 4);          // 7
 *   double s = uwl_call_f64(m, "scale", 3.0, 4.0);    // 12.0
 *   char  *g = uwl_call_str(m, "greet", "World");     // "Hello, World!"
 *   uwl_string_free(g);
 *
 * Each argument is read with the correct type from the export's parsed WIT
 * signature, so pass plain C values (int / int64_t / double / const char*).
 * Booleans are passed as `int`; `f32` params/args use C `double`/`float`.
 *
 * These REQUIRE a companion `.wit` (that is where the per-argument types come
 * from). For raw modules loaded without a `.wit`, use #uwl_call with explicit
 * #uwl_val_t values.
 *
 * Errors do not use an out-parameter: on failure the call returns the sentinel
 * (0 / NULL) and sets the thread-local last error, retrievable with
 * #uwl_last_error. A successful call clears it. The result-type suffix must
 * match the export's WIT result type; a mismatch is such a failure (e.g.
 * #uwl_call_i32 on an `f64` export returns 0 with the last error set); check
 * #uwl_last_error to tell a real 0 from a failure. #uwl_call_void discards any
 * result.
 */
int32_t uwl_call_i32 (uwl_module_t *m, const char *name, ...);
int64_t uwl_call_i64 (uwl_module_t *m, const char *name, ...);
float   uwl_call_f32 (uwl_module_t *m, const char *name, ...);
double  uwl_call_f64 (uwl_module_t *m, const char *name, ...);
int     uwl_call_bool(uwl_module_t *m, const char *name, ...);
void    uwl_call_void(uwl_module_t *m, const char *name, ...);
/** Like the others, but returns an owned heap string (free with
 *  #uwl_string_free), or NULL on failure / non-string result. */
char   *uwl_call_str (uwl_module_t *m, const char *name, ...);

/**
 * The last error from a convenience call (#uwl_call_i32 etc.) on the current
 * thread, or NULL if the last such call succeeded. Owned by the loader — do
 * not free; valid until this thread's next convenience call.
 */
const char *uwl_last_error(void);

/* ── Singleton (DLL pattern) ────────────────────────────────────────────── */

/** Lazily-loaded, cached single instance. See #uwl_singleton_get. */
typedef struct uwl_singleton uwl_singleton_t;

/**
 * Create a singleton accessor. The module is not loaded yet; the path and
 * callbacks are copied. Returns NULL only on allocation failure.
 */
uwl_singleton_t *uwl_singleton_new(const char *wasm_path,
                                   const uwl_host_callback_t *callbacks, size_t ncallbacks);

/**
 * Load the module on first call and cache it; subsequent calls return the same
 * pointer. Returns NULL with `*err` set on load failure. The returned module is
 * owned by the singleton — do not #uwl_free it; call #uwl_singleton_free.
 */
uwl_module_t *uwl_singleton_get(uwl_singleton_t *s, char **err);

/** Free the singleton and the cached module (if loaded). */
void uwl_singleton_free(uwl_singleton_t *s);

/* ── InstancePool (server / loop pattern) ───────────────────────────────── */

/**
 * A pool of independent pre-instantiated module instances.
 *
 * Distributing calls across N independent linear memories extends longevity
 * under wasic's bump allocator (which has no `free`).
 *
 * Note: this C pool is not internally synchronized. In a multithreaded host,
 * guard acquire/release with your own lock.
 */
typedef struct uwl_pool uwl_pool_t;

/**
 * Pre-instantiate `size` (default applied if 0 → 4) independent instances.
 * Returns NULL with `*err` set if any instance fails to load.
 */
uwl_pool_t *uwl_pool_new(const char *wasm_path,
                         const uwl_host_callback_t *callbacks, size_t ncallbacks,
                         size_t size, char **err);

/**
 * Check out an idle instance. Returns NULL with `*err` set if the pool is
 * exhausted. The returned module is owned by the pool — return it with
 * #uwl_pool_release; do not #uwl_free it.
 */
uwl_module_t *uwl_pool_acquire(uwl_pool_t *p, char **err);

/** Return a previously acquired instance to the pool. */
void uwl_pool_release(uwl_pool_t *p, uwl_module_t *m);

/** Total pool capacity. */
size_t uwl_pool_size(const uwl_pool_t *p);
/** Number of currently idle instances. */
size_t uwl_pool_available(const uwl_pool_t *p);

/** Free the pool and every instance it owns. */
void uwl_pool_free(uwl_pool_t *p);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 *                              IMPLEMENTATION
 * ========================================================================= */
#ifdef UWL_IMPLEMENTATION

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* wasm.h treats MinGW as plain `extern`, but wasi.h decorates every _WIN32
 * symbol `__declspec(dllimport)` unless WASI_API_EXTERN is pre-defined. For a
 * static link under MinGW (or any LIBWASM_STATIC build) make it plain extern so
 * the wasi_config_* symbols resolve against libwasmtime.a. */
#if !defined(WASI_API_EXTERN) && (defined(__MINGW32__) || defined(LIBWASM_STATIC))
#define WASI_API_EXTERN
#endif

#include <wasm.h>
#include <wasmtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── small utilities ────────────────────────────────────────────────────── */

static char *uwl__strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* A heap-allocated formatted message (NULL only when out of memory). */
static char *uwl__vfmt(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return uwl__strdup("(error formatting message)");
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf) vsnprintf(buf, (size_t)n + 1, fmt, ap);
    return buf;
}

static char *uwl__fmt(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *s = uwl__vfmt(fmt, ap);
    va_end(ap);
    return s;
}

/* Set *err to a heap-allocated formatted message (no-op if err is NULL). */
static void uwl__set_err(char **err, const char *fmt, ...) {
    if (!err) return;
    va_list ap; va_start(ap, fmt);
    *err = uwl__vfmt(fmt, ap);
    va_end(ap);
}

/* Copy a wasm_byte_vec_t (not NUL-terminated) into a fresh NUL-terminated string. */
static char *uwl__bytes_to_cstr(const wasm_byte_vec_t *v) {
    char *s = (char *)malloc(v->size + 1);
    if (!s) return NULL;
    memcpy(s, v->data, v->size);
    s[v->size] = '\0';
    return s;
}

/* Turn a wasmtime error into *err and delete it. */
static void uwl__err_from_wasmtime(char **err, const char *ctx, wasmtime_error_t *e) {
    wasm_byte_vec_t msg;
    wasmtime_error_message(e, &msg);
    char *m = uwl__bytes_to_cstr(&msg);
    uwl__set_err(err, "%s: %s", ctx, m ? m : "(unknown)");
    free(m);
    wasm_byte_vec_delete(&msg);
    wasmtime_error_delete(e);
}

static void uwl__err_from_trap(char **err, const char *ctx, wasm_trap_t *t) {
    wasm_byte_vec_t msg;
    wasm_trap_message(t, &msg);
    char *m = uwl__bytes_to_cstr(&msg);
    uwl__set_err(err, "%s: trap: %s", ctx, m ? m : "(unknown)");
    free(m);
    wasm_byte_vec_delete(&msg);
    wasm_trap_delete(t);
}

/* A host-side trap with a formatted message; it reaches the caller of the
 * export that called the import, through uwl_call's `err`. */
static wasm_trap_t *uwl__trap_fmt(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *s = uwl__vfmt(fmt, ap);
    va_end(ap);
    const char *msg = s ? s : "host callback failed (out of memory formatting the error)";
    wasm_trap_t *t = wasmtime_trap_new(msg, strlen(msg));
    free(s);
    return t;
}

/* ── WIT model ──────────────────────────────────────────────────────────── */

typedef enum { UWL_WT_S32, UWL_WT_S64, UWL_WT_F32, UWL_WT_F64, UWL_WT_BOOL, UWL_WT_STR } uwl_wt_t;

typedef struct {
    char    *kebab;   /* original WIT name */
    char    *camel;   /* camelCase (export name / user-facing import key) */
    char    *uskey;   /* underscore key (WASM import binary name) */
    uwl_wt_t *params; /* parameter types */
    char   **pnames;  /* parameter names, camelCase (for error messages) */
    size_t   nparams;
    int      has_result;
    uwl_wt_t result;
} uwl_witfunc_t;

typedef struct {
    uwl_witfunc_t *exports; size_t nexports;
    uwl_witfunc_t *imports; size_t nimports;
} uwl_wit_t;

/* SPEC 4.0.0 §2: only these six types; anything else (`u32`, `list<u8>`) is an
 * error, never read as another type. Returns 0 for an unsupported type. */
static int uwl__wt_parse(const char *s, size_t n, uwl_wt_t *out) {
    if (n == 3 && !strncmp(s, "s32", 3))    { *out = UWL_WT_S32;  return 1; }
    if (n == 3 && !strncmp(s, "s64", 3))    { *out = UWL_WT_S64;  return 1; }
    if (n == 3 && !strncmp(s, "f32", 3))    { *out = UWL_WT_F32;  return 1; }
    if (n == 3 && !strncmp(s, "f64", 3))    { *out = UWL_WT_F64;  return 1; }
    if (n == 4 && !strncmp(s, "bool", 4))   { *out = UWL_WT_BOOL; return 1; }
    if (n == 6 && !strncmp(s, "string", 6)) { *out = UWL_WT_STR;  return 1; }
    return 0;
}

static const char *uwl__wt_name(uwl_wt_t t) {
    switch (t) {
        case UWL_WT_S32:  return "s32";
        case UWL_WT_S64:  return "s64";
        case UWL_WT_F32:  return "f32";
        case UWL_WT_F64:  return "f64";
        case UWL_WT_BOOL: return "bool";
        case UWL_WT_STR:  return "string";
    }
    return "?";
}

/* The uwl_val_t kind that carries a WIT type. */
static uwl_kind_t uwl__kind_of_wt(uwl_wt_t t) {
    switch (t) {
        case UWL_WT_S32:  return UWL_I32;
        case UWL_WT_S64:  return UWL_I64;
        case UWL_WT_F32:  return UWL_F32;
        case UWL_WT_F64:  return UWL_F64;
        case UWL_WT_BOOL: return UWL_BOOL;
        case UWL_WT_STR:  return UWL_STR;
    }
    return UWL_VOID;
}

/* A kind by its WIT name ("void" for UWL_VOID). */
static const char *uwl__kind_name(uwl_kind_t k) {
    switch (k) {
        case UWL_VOID: return "void";
        case UWL_I32:  return "s32";
        case UWL_I64:  return "s64";
        case UWL_F32:  return "f32";
        case UWL_F64:  return "f64";
        case UWL_BOOL: return "bool";
        case UWL_STR:  return "string";
        case UWL_ERR:  return "error";
    }
    return "?";
}

/* The constructor that builds a value of a kind, for error messages. */
static const char *uwl__kind_ctor(uwl_kind_t k) {
    switch (k) {
        case UWL_VOID: return "uwl_void";
        case UWL_I32:  return "uwl_i32";
        case UWL_I64:  return "uwl_i64";
        case UWL_F32:  return "uwl_f32";
        case UWL_F64:  return "uwl_f64";
        case UWL_BOOL: return "uwl_bool";
        case UWL_STR:  return "uwl_str";
        case UWL_ERR:  return "uwl_err";
    }
    return "?";
}

/* A value as text with its kind, e.g. `2.9 (f64)`, `"17" (string)`. */
static char *uwl__show_val(const uwl_val_t *v) {
    switch (v->kind) {
        case UWL_VOID: return uwl__strdup("no value (void)");
        case UWL_I32:  return uwl__fmt("%d (s32)", (int)v->as.i32);
        case UWL_I64:  return uwl__fmt("%lld (s64)", (long long)v->as.i64);
        case UWL_F32:  return uwl__fmt("%g (f32)", (double)v->as.f32);
        case UWL_F64:  return uwl__fmt("%g (f64)", v->as.f64);
        case UWL_BOOL: return uwl__fmt("%s (bool)", v->as.b ? "true" : "false");
        case UWL_STR:  return uwl__fmt("\"%.*s\" (string)", (int)(v->len > 60 ? 60 : v->len),
                                       v->str ? v->str : "");
        case UWL_ERR:  return uwl__fmt("an error (%s)", v->str ? v->str : "no message");
    }
    return uwl__fmt("an invalid value (kind %d)", (int)v->kind);
}

/* kebab-case → camelCase as the reference does: a '-' followed by [a-z0-9] is
 * dropped and the next letter upper-cased; any other '-' stays. */
static char *uwl__kebab_to_camel(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '-' && i + 1 < n &&
            ((s[i + 1] >= 'a' && s[i + 1] <= 'z') || (s[i + 1] >= '0' && s[i + 1] <= '9'))) {
            c = s[++i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        }
        out[j++] = c;
    }
    out[j] = '\0';
    return out;
}

static char *uwl__kebab_to_underscore(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) out[i] = (s[i] == '-') ? '_' : s[i];
    out[n] = '\0';
    return out;
}

/* ── strict WIT parser (SPEC 4.0.0 §2 "Parsing") ─────────────────────────────
 *
 *   wit   ::= ("package" name ";")? "world" ident "{" (func ";")* "}"
 *   func  ::= ("import" | "export") ident ":" "func" "(" (param ("," param)* ","?)? ")" ("->" type)?
 *   param ::= ident ":" type
 *   type  ::= s32 | s64 | f32 | f64 | bool | string
 *
 * `//` comments anywhere. Anything else fails with a reason (the caller names
 * the .wit). The same accepted forms and messages as the reference
 * (universalWasmLoader-js `wit-parser.js`). Texts are (pointer, length) pairs:
 * nothing below relies on a NUL terminator. */

static int uwl__ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int uwl__alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* ident: [A-Za-z][A-Za-z0-9]*(-[A-Za-z0-9]+)* */
static int uwl__ident_ok(const char *s, size_t n) {
    if (n == 0 || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return 0;
    for (size_t i = 1; i < n; i++) {
        if (s[i] == '-') { if (i + 1 >= n || !uwl__alnum(s[i + 1])) return 0; }
        else if (!uwl__alnum(s[i])) return 0;
    }
    return 1;
}

static void uwl__trim(const char **s, size_t *n) {
    while (*n && uwl__ws(**s)) { (*s)++; (*n)--; }
    while (*n && uwl__ws((*s)[*n - 1])) (*n)--;
}

static int uwl__word_at(const char *s, size_t n, const char *word) {
    size_t w = strlen(word);
    return n > w && !memcmp(s, word, w) && uwl__ws(s[w]);
}

/* A run of characters until whitespace or any of `stops`. */
static size_t uwl__run(const char *s, size_t n, size_t i, const char *stops) {
    while (i < n && !uwl__ws(s[i]) && !strchr(stops, s[i])) i++;
    return i;
}

static size_t uwl__skip_ws(const char *s, size_t n, size_t i) {
    while (i < n && uwl__ws(s[i])) i++;
    return i;
}

/* Statement text with whitespace runs collapsed to one space (for messages). */
static char *uwl__collapse(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (uwl__ws(s[i])) { if (j && out[j - 1] != ' ') out[j++] = ' '; }
        else out[j++] = s[i];
    }
    out[j] = '\0';
    return out;
}

static char *uwl__dupn(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

static void uwl__witfunc_free(uwl_witfunc_t *f) {
    if (f->pnames) for (size_t i = 0; i < f->nparams; i++) free(f->pnames[i]);
    free(f->pnames);
    free(f->kebab); free(f->camel); free(f->uskey); free(f->params);
}

/* Parse "a: s32, b: f64" into f->params / f->pnames. `label` is e.g.
 * "export add". Returns 0 with *why set on a fault. */
static int uwl__parse_params(const char *s, size_t n, uwl_witfunc_t *f,
                             const char *label, char **why) {
    size_t cap = 1;
    for (size_t i = 0; i < n; i++) if (s[i] == ',') cap++;
    f->params = (uwl_wt_t *)malloc(cap * sizeof(uwl_wt_t));
    f->pnames = (char **)calloc(cap, sizeof(char *));
    const char **raw = (const char **)malloc(cap * sizeof(char *));
    size_t *rawlen = (size_t *)malloc(cap * sizeof(size_t));
    int ok = 0;
    if (!f->params || !f->pnames || !raw || !rawlen) { uwl__set_err(why, "out of memory"); goto done; }

    for (size_t start = 0; start <= n;) {
        size_t end = start;
        while (end < n && s[end] != ',') end++;
        const char *part = s + start; size_t plen = end - start;
        uwl__trim(&part, &plen);
        int last = (end >= n);
        /* an empty LAST part is `func()` or a trailing comma `func(a: s32,)`;
           an empty part anywhere else is malformed (below) */
        if (plen == 0 && last) break;
        /* ^([^:\s]+)\s*:\s*(\S+)$ */
        size_t i = uwl__run(part, plen, 0, ":");
        size_t name_end = i;
        i = uwl__skip_ws(part, plen, i);
        int shape = name_end > 0 && i < plen && part[i] == ':';
        size_t ts = 0, te = 0;
        if (shape) {
            ts = uwl__skip_ws(part, plen, i + 1);
            te = uwl__run(part, plen, ts, "");
            shape = te > ts && te == plen;
        }
        if (!shape || !uwl__ident_ok(part, name_end)) {
            uwl__set_err(why, "malformed parameter \"%.*s\" in %s (expected \"name: type\")",
                         (int)plen, part, label);
            goto done;
        }
        for (size_t p = 0; p < f->nparams; p++) {
            if (rawlen[p] == name_end && !memcmp(raw[p], part, name_end)) {
                uwl__set_err(why, "duplicate parameter \"%.*s\" in %s", (int)name_end, part, label);
                goto done;
            }
        }
        uwl_wt_t t;
        if (!uwl__wt_parse(part + ts, te - ts, &t)) {
            uwl__set_err(why, "unsupported WIT type \"%.*s\" in %s, parameter %.*s "
                         "(supported: s32, s64, f32, f64, bool, string)",
                         (int)(te - ts), part + ts, label, (int)name_end, part);
            goto done;
        }
        raw[f->nparams] = part; rawlen[f->nparams] = name_end;
        f->pnames[f->nparams] = uwl__kebab_to_camel(part, name_end);
        f->params[f->nparams++] = t;
        if (last) break;
        start = end + 1;
    }
    ok = 1;
done:
    free(raw); free(rawlen);
    return ok;
}

/* One statement of the world body, trimmed, without its ';'.
 * ^(import|export)\s+([^\s:]+)\s*:\s*func\s*\(([^()]*)\)\s*(?:->\s*(\S+))?$ */
static int uwl__parse_stmt(const char *s, size_t n, uwl_wit_t *wit, char **why) {
    /* declared up front: no goto below jumps past an initialisation (C++) */
    int is_import = 0, has_result = 0;
    size_t i, ns, ne, ps, pe, rs = 0, re = 0;
    if (uwl__word_at(s, n, "import")) is_import = 1;
    else if (uwl__word_at(s, n, "export")) is_import = 0;
    else goto bad;
    i = uwl__skip_ws(s, n, 6);
    ns = i; i = uwl__run(s, n, i, ":"); ne = i;
    if (ne == ns) goto bad;
    i = uwl__skip_ws(s, n, i);
    if (i >= n || s[i] != ':') goto bad;
    i = uwl__skip_ws(s, n, i + 1);
    if (n - i < 4 || memcmp(s + i, "func", 4)) goto bad;
    i = uwl__skip_ws(s, n, i + 4);
    if (i >= n || s[i] != '(') goto bad;
    ps = ++i;
    while (i < n && s[i] != '(' && s[i] != ')') i++;
    if (i >= n || s[i] != ')') goto bad;
    pe = i++;
    i = uwl__skip_ws(s, n, i);
    if (i < n) {
        if (n - i < 2 || s[i] != '-' || s[i + 1] != '>') goto bad;
        rs = uwl__skip_ws(s, n, i + 2);
        re = uwl__run(s, n, rs, "");
        if (re == rs || re != n) goto bad;
        has_result = 1;
    }

    {
        const char *kw = is_import ? "import" : "export";
        char *label = uwl__fmt("%s %.*s", kw, (int)(ne - ns), s + ns);
        if (!label) { uwl__set_err(why, "out of memory"); return 0; }
        uwl_witfunc_t **list = is_import ? &wit->imports : &wit->exports;
        size_t *count = is_import ? &wit->nimports : &wit->nexports;
        int ok = 0;
        uwl_witfunc_t f; memset(&f, 0, sizeof(f));
        uwl_witfunc_t *grown = NULL;

        if (!uwl__ident_ok(s + ns, ne - ns)) { uwl__set_err(why, "malformed function name in %s", label); goto out; }
        for (size_t k = 0; k < *count; k++) {
            if (strlen((*list)[k].kebab) == ne - ns && !memcmp((*list)[k].kebab, s + ns, ne - ns)) {
                uwl__set_err(why, "duplicate %s", label); goto out;
            }
        }
        if (!uwl__parse_params(s + ps, pe - ps, &f, label, why)) goto out;
        if (has_result) {
            if (!uwl__wt_parse(s + rs, re - rs, &f.result)) {
                uwl__set_err(why, "unsupported WIT type \"%.*s\" in %s, result "
                             "(supported: s32, s64, f32, f64, bool, string)",
                             (int)(re - rs), s + rs, label);
                goto out;
            }
            f.has_result = 1;
        }
        f.kebab = uwl__dupn(s + ns, ne - ns);
        f.camel = uwl__kebab_to_camel(s + ns, ne - ns);
        f.uskey = uwl__kebab_to_underscore(s + ns, ne - ns);
        grown = (uwl_witfunc_t *)realloc(*list, (*count + 1) * sizeof(uwl_witfunc_t));
        if (!f.kebab || !f.camel || !f.uskey || !grown) {
            if (grown) *list = grown;
            uwl__set_err(why, "out of memory"); goto out;
        }
        *list = grown;
        (*list)[(*count)++] = f;
        memset(&f, 0, sizeof(f)); /* now owned by the list */
        ok = 1;
    out:
        uwl__witfunc_free(&f);
        free(label);
        return ok;
    }

bad: {
        char *shown = uwl__collapse(s, n);
        uwl__set_err(why, "unsupported or malformed WIT statement: %s", shown ? shown : "?");
        free(shown);
        return 0;
    }
}

static void uwl__wit_free(uwl_wit_t *wit);

/* Parse `src` (`len` bytes). Returns 1, or 0 with *why set and `wit` empty. */
static int uwl__parse_wit(const char *src, size_t len, uwl_wit_t *wit, char **why) {
    memset(wit, 0, sizeof(*wit));
    if (memchr(src, '\0', len)) { uwl__set_err(why, "not a text file (it contains a NUL byte)"); return 0; }
    size_t off = (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB &&
                  (unsigned char)src[2] == 0xBF) ? 3 : 0;
    size_t n = len - off;
    char *code = (char *)malloc(n + 1);
    if (!code) { uwl__set_err(why, "out of memory"); return 0; }
    for (size_t i = 0; i < n;) {   /* blank out `//` comments */
        if (src[off + i] == '/' && i + 1 < n && src[off + i + 1] == '/') {
            while (i < n && src[off + i] != '\n') code[i++] = ' ';
        } else { code[i] = src[off + i]; i++; }
    }
    code[n] = '\0';

    int ok = 0;
    const char *p = code; size_t pn = n;
    uwl__trim(&p, &pn);

    /* ("package" name ";")? — a malformed one is left for the world check */
    if (uwl__word_at(p, pn, "package")) {
        size_t i = uwl__skip_ws(p, pn, 7);
        size_t ns = i; i = uwl__run(p, pn, i, ";"); size_t ne = i;
        i = uwl__skip_ws(p, pn, i);
        if (ne > ns && i < pn && p[i] == ';') { p += i + 1; pn -= i + 1; uwl__trim(&p, &pn); }
    }

    /* ^world\s+([^\s{]+)\s*\{([^{}]*)\}$ */
    size_t ws_ = 0, we = 0, bs = 0, be = 0;
    int shape = uwl__word_at(p, pn, "world");
    if (shape) {
        size_t i = uwl__skip_ws(p, pn, 5);
        ws_ = i; i = uwl__run(p, pn, i, "{"); we = i;
        i = uwl__skip_ws(p, pn, i);
        shape = we > ws_ && i < pn && p[i] == '{';
        if (shape) {
            bs = i + 1; be = bs;
            while (be < pn && p[be] != '{' && p[be] != '}') be++;
            shape = be < pn && p[be] == '}' && be == pn - 1;
        }
    }
    if (!shape) {
        if (pn == 0) uwl__set_err(why, "not a WIT world: it is empty");
        else uwl__set_err(why, "not a WIT world: expected one \"world <name> { ... }\" block, found: %.*s",
                          (int)(pn > 60 ? 60 : pn), p);
        goto done;
    }
    if (!uwl__ident_ok(p + ws_, we - ws_)) {
        uwl__set_err(why, "malformed world name \"%.*s\"", (int)(we - ws_), p + ws_);
        goto done;
    }

    {   /* statements: the text after the last ';' must be blank */
        size_t last = be;
        for (size_t i = bs; i < be; i++) if (p[i] == ';') last = i;
        const char *tail = p + (last == be ? bs : last + 1);
        size_t tn = (size_t)((p + be) - tail);
        uwl__trim(&tail, &tn);
        if (tn) {
            char *shown = uwl__collapse(tail, tn);
            uwl__set_err(why, "unterminated WIT statement (missing \";\"): %s", shown ? shown : "?");
            free(shown);
            goto done;
        }
        for (size_t s = bs; last != be && s <= last;) {
            size_t e = s;
            while (p[e] != ';') e++;
            const char *st = p + s; size_t sn = e - s;
            uwl__trim(&st, &sn);
            if (!uwl__parse_stmt(st, sn, wit, why)) goto done;
            s = e + 1;
        }
    }
    ok = 1;
done:
    free(code);
    if (!ok) uwl__wit_free(wit);
    return ok;
}

static void uwl__wit_free(uwl_wit_t *wit) {
    for (size_t i = 0; i < wit->nexports; i++) uwl__witfunc_free(&wit->exports[i]);
    for (size_t i = 0; i < wit->nimports; i++) uwl__witfunc_free(&wit->imports[i]);
    free(wit->exports); free(wit->imports);
    memset(wit, 0, sizeof(*wit));
}

/* ── module handle ──────────────────────────────────────────────────────── */

typedef struct uwl_binding uwl_binding_t; /* fwd */

struct uwl_module {
    wasm_engine_t      *engine;
    wasmtime_store_t   *store;
    wasmtime_context_t *ctx;
    wasmtime_module_t  *module;
    wasmtime_instance_t instance;
    wasmtime_memory_t   memory;
    int                 has_memory;
    uwl_wit_t           wit;
    int                 has_wit;
    uwl_binding_t      *bindings; /* one per WIT import; stable for store lifetime */
    size_t              nbindings;
};

struct uwl_binding {
    uwl_module_t       *mod;
    const uwl_witfunc_t *fn;
    uwl_host_fn_t        user_fn;
    void                *userdata;
};

/* ── import trampoline ──────────────────────────────────────────────────── */

static wasm_valtype_t *uwl__wt_to_valtype(uwl_wt_t t) {
    switch (t) {
        case UWL_WT_S64: return wasm_valtype_new(WASM_I64);
        case UWL_WT_F32: return wasm_valtype_new(WASM_F32);
        case UWL_WT_F64: return wasm_valtype_new(WASM_F64);
        /* s32 / bool / string(ptr+len) all use i32 lanes */
        default:         return wasm_valtype_new(WASM_I32);
    }
}

static wasm_functype_t *uwl__functype_for(const uwl_witfunc_t *f) {
    /* params: string → 2×i32, else 1 */
    size_t np = 0;
    for (size_t i = 0; i < f->nparams; i++) np += (f->params[i] == UWL_WT_STR) ? 2 : 1;

    wasm_valtype_vec_t params, results;
    if (np == 0) {
        wasm_valtype_vec_new_empty(&params);
    } else {
        wasm_valtype_t **ps = (wasm_valtype_t **)malloc(np * sizeof(*ps));
        size_t j = 0;
        for (size_t i = 0; i < f->nparams; i++) {
            if (f->params[i] == UWL_WT_STR) {
                ps[j++] = wasm_valtype_new(WASM_I32);
                ps[j++] = wasm_valtype_new(WASM_I32);
            } else {
                ps[j++] = uwl__wt_to_valtype(f->params[i]);
            }
        }
        wasm_valtype_vec_new(&params, np, ps);
        free(ps);
    }

    if (!f->has_result) {
        wasm_valtype_vec_new_empty(&results);
    } else {
        wasm_valtype_t *r[1];
        r[0] = uwl__wt_to_valtype(f->result); /* string return = i32 ptr */
        wasm_valtype_vec_new(&results, 1, r);
    }
    return wasm_functype_new(&params, &results);
}

static wasm_trap_t *uwl__import_trampoline(void *env, wasmtime_caller_t *caller,
                                           const wasmtime_val_t *args, size_t nargs,
                                           wasmtime_val_t *results, size_t nresults) {
    (void)nargs;
    uwl_binding_t *b = (uwl_binding_t *)env;
    const uwl_witfunc_t *f = b->fn;

    /* Resolve caller memory for string decoding. */
    uint8_t *mem_data = NULL; size_t mem_size = 0;
    {
        wasmtime_extern_t ext;
        if (wasmtime_caller_export_get(caller, "memory", 6, &ext)) {
            if (ext.kind == WASMTIME_EXTERN_MEMORY) {
                wasmtime_context_t *cx = wasmtime_caller_context(caller);
                mem_data = wasmtime_memory_data(cx, &ext.of.memory);
                mem_size = wasmtime_memory_data_size(cx, &ext.of.memory);
            }
            wasmtime_extern_delete(&ext);
        }
    }

    /* Every import has a callback (uwl__bind_imports refuses the load
       otherwise); this guards a binding built some other way. */
    if (!b->user_fn) return uwl__trap_fmt("no host callback for WIT import: %s (WIT \"%s\")", f->camel, f->kebab);

    /* Decode arguments. */
    uwl_val_t stackvals[8];
    uwl_val_t *vals = (f->nparams <= 8) ? stackvals
                                        : (uwl_val_t *)malloc(f->nparams * sizeof(uwl_val_t));
    if (!vals) return uwl__trap_fmt("out of memory decoding the arguments of import \"%s\"", f->kebab);
    size_t ai = 0, nvals = 0;
    wasm_trap_t *fault = NULL;
    for (size_t i = 0; i < f->nparams && !fault; i++) {
        switch (f->params[i]) {
            case UWL_WT_STR: {
                int32_t ptr = args[ai++].of.i32;
                int32_t len = args[ai++].of.i32;
                if (!mem_data) {
                    fault = uwl__trap_fmt("import \"%s\" received a string but the module exports no memory", f->kebab);
                } else if (ptr < 0 || len < 0 || (size_t)ptr + (size_t)len > mem_size) {
                    fault = uwl__trap_fmt("import \"%s\" received a string out of bounds (ptr %d, len %d, memory %zu bytes)",
                                          f->kebab, (int)ptr, (int)len, mem_size);
                } else {
                    vals[nvals++] = uwl_strn((const char *)(mem_data + ptr), (size_t)len);
                }
                break;
            }
            case UWL_WT_BOOL: vals[nvals++] = uwl_bool(args[ai++].of.i32 != 0); break;
            case UWL_WT_S64:  vals[nvals++] = uwl_i64(args[ai++].of.i64); break;
            case UWL_WT_F32:  vals[nvals++] = uwl_f32(args[ai++].of.f32); break;
            case UWL_WT_F64:  vals[nvals++] = uwl_f64(args[ai++].of.f64); break;
            default:          vals[nvals++] = uwl_i32(args[ai++].of.i32); break; /* s32 */
        }
    }

    uwl_val_t ret = uwl_void();
    if (!fault) ret = b->user_fn(vals, f->nparams, b->userdata);

    for (size_t i = 0; i < nvals; i++) uwl_val_free(&vals[i]);
    if (vals != stackvals) free(vals);
    if (fault) return fault;

    /* The callback FAILED (uwl_err): trap with its message, for every import,
       void ones too, so the WASM stops here (workspace C39). */
    if (ret.kind == UWL_ERR) {
        wasm_trap_t *t = uwl__trap_fmt("host callback %s (WIT import \"%s\") failed: %s",
                                       f->camel, f->kebab, ret.str ? ret.str : "(no message)");
        uwl_val_free(&ret);
        return t;
    }

    /* SPEC 4.0.0 §4: the return value must be the import's WIT result kind,
       never coerced. A void import ignores it. A `string` result never gets
       here: such an import is refused at load. */
    if (f->has_result) {
        uwl_kind_t want = uwl__kind_of_wt(f->result);
        if (ret.kind != want || nresults < 1) {
            char *shown = uwl__show_val(&ret);
            wasm_trap_t *t = uwl__trap_fmt(
                "host callback %s (WIT import \"%s\" -> %s) returned %s; expected %s (%s)",
                f->camel, f->kebab, uwl__wt_name(f->result), shown ? shown : "?",
                uwl__kind_name(want), uwl__kind_ctor(want));
            free(shown);
            uwl_val_free(&ret);
            return t;
        }
        switch (f->result) {
            case UWL_WT_BOOL: results[0].kind = WASMTIME_I32; results[0].of.i32 = ret.as.b ? 1 : 0; break;
            case UWL_WT_S64:  results[0].kind = WASMTIME_I64; results[0].of.i64 = ret.as.i64; break;
            case UWL_WT_F32:  results[0].kind = WASMTIME_F32; results[0].of.f32 = ret.as.f32; break;
            case UWL_WT_F64:  results[0].kind = WASMTIME_F64; results[0].of.f64 = ret.as.f64; break;
            default:          results[0].kind = WASMTIME_I32; results[0].of.i32 = ret.as.i32; break;
        }
    }
    uwl_val_free(&ret);
    return NULL;
}

/* A comma-separated "camel (WIT \"kebab\")" list of the selected functions. */
static char *uwl__func_list(const uwl_witfunc_t *const *sel, size_t n) {
    size_t cap = 1;
    for (size_t i = 0; i < n; i++) cap += strlen(sel[i]->camel) + strlen(sel[i]->kebab) + 12;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++)
        j += (size_t)snprintf(out + j, cap - j, "%s%s (WIT \"%s\")", i ? ", " : "",
                              sel[i]->camel, sel[i]->kebab);
    out[j] = '\0';
    return out;
}

/* SPEC 4.0.0 §4: bind a callback to every WIT import, before the .wasm is
 * read. Refuses, naming every one, an import with no callback (a NULL `fn`
 * counts as none) and an import with a `string` result (not supported by this
 * ABI profile). Callbacks for names the .wit does not import are ignored. */
static int uwl__bind_imports(uwl_module_t *m, const uwl_host_callback_t *callbacks,
                             size_t ncallbacks, char **err) {
    size_t n = m->wit.nimports;
    if (!n) return 1;
    m->bindings = (uwl_binding_t *)calloc(n, sizeof(uwl_binding_t));
    const uwl_witfunc_t **missing = (const uwl_witfunc_t **)malloc(n * sizeof(*missing));
    const uwl_witfunc_t **strres = (const uwl_witfunc_t **)malloc(n * sizeof(*strres));
    if (!m->bindings || !missing || !strres) {
        free(missing); free(strres);
        uwl__set_err(err, "out of memory"); return 0;
    }
    m->nbindings = n;
    size_t nmissing = 0, nstr = 0;
    for (size_t i = 0; i < n; i++) {
        const uwl_witfunc_t *f = &m->wit.imports[i];
        uwl_binding_t *b = &m->bindings[i];
        b->mod = m; b->fn = f;
        for (size_t c = 0; c < ncallbacks && callbacks; c++) {
            if (callbacks[c].name && callbacks[c].fn && !strcmp(callbacks[c].name, f->camel)) {
                b->user_fn = callbacks[c].fn; b->userdata = callbacks[c].userdata; break;
            }
        }
        if (!b->user_fn) missing[nmissing++] = f;
        if (f->has_result && f->result == UWL_WT_STR) strres[nstr++] = f;
    }
    int ok = 1;
    if (nmissing) {
        char *names = uwl__func_list(missing, nmissing);
        uwl__set_err(err, "no host callback for WIT import%s: %s", nmissing > 1 ? "s" : "", names ? names : "?");
        free(names); ok = 0;
    } else if (nstr) {
        char *names = uwl__func_list(strres, nstr);
        uwl__set_err(err, "a WIT import returning a string is not supported: %s", names ? names : "?");
        free(names); ok = 0;
    }
    free(missing); free(strres);
    return ok;
}

/* ── path handling ──────────────────────────────────────────────────────── */

/* Returns a heap copy of the path with any "@N" suffix stripped, and writes
   the requested version (or -1 if absent) to *out_version. */
static char *uwl__strip_version(const char *path, long *out_version) {
    *out_version = -1;
    const char *at = strrchr(path, '@');
    if (at) {
        const char *d = at + 1;
        if (*d) {
            int all_digits = 1;
            for (const char *p = d; *p; p++) if (*p < '0' || *p > '9') { all_digits = 0; break; }
            if (all_digits) {
                *out_version = strtol(d, NULL, 10);
                size_t n = (size_t)(at - path);
                char *clean = (char *)malloc(n + 1);
                memcpy(clean, path, n); clean[n] = '\0';
                return clean;
            }
        }
    }
    return uwl__strdup(path);
}

/* The companion .wit path of a clean .wasm path: the trailing `.wasm` replaced
 * by `.wit`. A path that does not end in `.wasm` has no companion (SPEC §2):
 * NULL, and *oom stays 0. NULL with *oom = 1 is an allocation failure. */
static char *uwl__wit_path(const char *wasm_path, int *oom) {
    *oom = 0;
    size_t n = strlen(wasm_path);
    if (n < 5 || strcmp(wasm_path + n - 5, ".wasm") != 0) return NULL;
    char *w = (char *)malloc(n);
    if (!w) { *oom = 1; return NULL; }
    memcpy(w, wasm_path, n - 5);
    memcpy(w + n - 5, ".wit", 5); /* includes NUL */
    return w;
}

/* Read a whole file. NULL on failure with *out_errno set (ENOENT: it does not
 * exist). Reads in chunks rather than trusting ftell, so a directory or an
 * unreadable stream fails here instead of reading as empty. */
static char *uwl__read_file(const char *path, size_t *out_len, int *out_errno) {
    *out_errno = 0;
    errno = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) { *out_errno = errno ? errno : EIO; return NULL; }
    size_t cap = 4096, n = 0;
    char *buf = (char *)malloc(cap + 1);
    if (!buf) { fclose(fp); *out_errno = ENOMEM; return NULL; }
    for (;;) {
        if (n == cap) {
            char *nb = (char *)realloc(buf, cap * 2 + 1);
            if (!nb) { free(buf); fclose(fp); *out_errno = ENOMEM; return NULL; }
            buf = nb; cap *= 2;
        }
        errno = 0;
        size_t rd = fread(buf + n, 1, cap - n, fp);
        n += rd;
        if (rd == 0) break;
    }
    if (ferror(fp)) {
        *out_errno = errno ? errno : EIO;
        free(buf); fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* ── loading ────────────────────────────────────────────────────────────── */

static int uwl__lookup_func(uwl_module_t *m, const char *name, size_t namelen,
                            wasmtime_func_t *out) {
    wasmtime_extern_t ext;
    if (!wasmtime_instance_export_get(m->ctx, &m->instance, name, namelen, &ext)) return 0;
    int ok = (ext.kind == WASMTIME_EXTERN_FUNC);
    if (ok) *out = ext.of.func;
    wasmtime_extern_delete(&ext);
    return ok;
}

uwl_module_t *uwl_import(const char *wasm_path,
                         const uwl_host_callback_t *callbacks, size_t ncallbacks,
                         char **err) {
    if (err) *err = NULL;
    if (!wasm_path) { uwl__set_err(err, "null wasm_path"); return NULL; }
    long want_version = -1;
    char *clean = uwl__strip_version(wasm_path, &want_version);
    if (!clean) { uwl__set_err(err, "out of memory"); return NULL; }

    size_t wasm_len = 0;
    char *wasm_bytes = NULL;
    wasmtime_error_t *werr = NULL;   /* declared before any goto (C++) */
    wasmtime_linker_t *linker = NULL;
    uwl_module_t *m = (uwl_module_t *)calloc(1, sizeof(uwl_module_t));
    if (!m) { uwl__set_err(err, "out of memory"); free(clean); return NULL; }

    /* SPEC 4.0.0 §2: the .wit first. Only an ABSENT one (it does not exist)
       selects raw exports; any other read failure, and any .wit that does not
       parse, fails the load before the .wasm is read. */
    {
        int oom = 0, eno = 0;
        char *witp = uwl__wit_path(clean, &oom);
        if (oom) { uwl__set_err(err, "out of memory"); goto fail; }
        if (witp) {
            size_t wl = 0;
            char *src = uwl__read_file(witp, &wl, &eno);
            if (!src && eno != ENOENT) {
                uwl__set_err(err, "cannot read .wit '%s': %s", witp, strerror(eno));
                free(witp); goto fail;
            }
            if (src) {
                char *why = NULL;
                int ok = uwl__parse_wit(src, wl, &m->wit, &why);
                free(src);
                if (!ok) {
                    uwl__set_err(err, "invalid .wit '%s': %s", witp, why ? why : "out of memory");
                    free(why); free(witp); goto fail;
                }
                m->has_wit = 1;
            }
            free(witp);
        }
    }

    /* SPEC 4.0.0 §4: every WIT import has a callback, checked before the
       .wasm is read. */
    if (m->has_wit && !uwl__bind_imports(m, callbacks, ncallbacks, err)) goto fail;

    {
        int eno = 0;
        wasm_bytes = uwl__read_file(clean, &wasm_len, &eno);
        if (!wasm_bytes) { uwl__set_err(err, "failed to read '%s': %s", clean, strerror(eno)); goto fail; }
    }

    m->engine = wasm_engine_new();
    if (!m->engine) { uwl__set_err(err, "wasm_engine_new failed"); goto fail; }

    werr = wasmtime_module_new(m->engine, (const uint8_t *)wasm_bytes, wasm_len, &m->module);
    if (werr) { uwl__err_from_wasmtime(err, "compile", werr); goto fail; }

    m->store = wasmtime_store_new(m->engine, NULL, NULL);
    if (!m->store) { uwl__set_err(err, "wasmtime_store_new failed"); goto fail; }
    m->ctx = wasmtime_store_context(m->store);

    /* WASI (SPEC §10): always configure so I/O-using libraries instantiate. */
    {
        wasi_config_t *wcfg = wasi_config_new();
        if (wcfg) {
            wasi_config_inherit_stdout(wcfg);
            wasi_config_inherit_stderr(wcfg);
            wasmtime_error_t *we = wasmtime_context_set_wasi(m->ctx, wcfg);
            if (we) wasmtime_error_delete(we); /* tolerate: pure-compute modules don't need it */
        }
    }

    linker = wasmtime_linker_new(m->engine);
    if (!linker) { uwl__set_err(err, "wasmtime_linker_new failed"); goto fail; }
    {
        wasmtime_error_t *we = wasmtime_linker_define_wasi(linker);
        if (we) wasmtime_error_delete(we);
    }

    /* Define the bound host callbacks under "env" for each WIT import. */
    if (m->has_wit) {
        for (size_t i = 0; i < m->nbindings; i++) {
            const uwl_witfunc_t *f = m->bindings[i].fn;
            uwl_binding_t *b = &m->bindings[i];
            wasm_functype_t *ft = uwl__functype_for(f);
            wasmtime_error_t *we = wasmtime_linker_define_func(
                linker, "env", 3, f->uskey, strlen(f->uskey), ft,
                uwl__import_trampoline, b, NULL);
            wasm_functype_delete(ft);
            if (we) { uwl__err_from_wasmtime(err, "define import", we); wasmtime_linker_delete(linker); goto fail; }
        }
    }

    /* Satisfy any remaining imports with traps so instantiation can proceed. */
    {
        wasmtime_error_t *we = wasmtime_linker_define_unknown_imports_as_traps(linker, m->module);
        if (we) wasmtime_error_delete(we);
    }

    {
        wasm_trap_t *trap = NULL;
        wasmtime_error_t *we = wasmtime_linker_instantiate(linker, m->ctx, m->module, &m->instance, &trap);
        wasmtime_linker_delete(linker);
        if (we) { uwl__err_from_wasmtime(err, "instantiate", we); goto fail; }
        if (trap) { uwl__err_from_trap(err, "instantiate", trap); goto fail; }
    }

    /* Cache memory export. */
    {
        wasmtime_extern_t ext;
        if (wasmtime_instance_export_get(m->ctx, &m->instance, "memory", 6, &ext)) {
            if (ext.kind == WASMTIME_EXTERN_MEMORY) { m->memory = ext.of.memory; m->has_memory = 1; }
            wasmtime_extern_delete(&ext);
        }
    }

    /* SPEC §10.1: call reactor _initialize once if present. */
    {
        wasmtime_func_t init;
        if (uwl__lookup_func(m, "_initialize", 11, &init)) {
            wasm_trap_t *trap = NULL;
            wasmtime_error_t *we = wasmtime_func_call(m->ctx, &init, NULL, 0, NULL, 0, &trap);
            if (we) { uwl__err_from_wasmtime(err, "_initialize", we); goto fail; }
            if (trap) { uwl__err_from_trap(err, "_initialize", trap); goto fail; }
        }
    }

    /* §3: version pinning. */
    if (want_version >= 0) {
        wasmtime_extern_t ext;
        if (!wasmtime_instance_export_get(m->ctx, &m->instance, "version", 7, &ext) ||
            ext.kind != WASMTIME_EXTERN_GLOBAL) {
            uwl__set_err(err, "version @%ld requested for '%s' but the module exports no 'version' global",
                         want_version, clean);
            if (ext.kind != WASMTIME_EXTERN_GLOBAL) {} /* nothing extra */
            goto fail;
        }
        wasmtime_val_t v;
        wasmtime_global_get(m->ctx, &ext.of.global, &v);
        wasmtime_extern_delete(&ext);
        int32_t actual = (v.kind == WASMTIME_I32) ? v.of.i32 : -1;
        wasmtime_val_unroot(&v); /* no-op for i32, kept for correctness */
        if (actual != (int32_t)want_version) {
            uwl__set_err(err, "version mismatch for '%s' — requested @%ld, module exports version %d",
                         clean, want_version, actual);
            goto fail;
        }
    }

    /* SPEC 4.0.0 §2: a WIT export the module does not export fails the load,
       naming every one. */
    if (m->has_wit && m->wit.nexports) {
        const uwl_witfunc_t **absent =
            (const uwl_witfunc_t **)malloc(m->wit.nexports * sizeof(*absent));
        if (!absent) { uwl__set_err(err, "out of memory"); goto fail; }
        size_t nabsent = 0;
        for (size_t i = 0; i < m->wit.nexports; i++) {
            wasmtime_func_t fn;
            const uwl_witfunc_t *f = &m->wit.exports[i];
            if (!uwl__lookup_func(m, f->camel, strlen(f->camel), &fn)) absent[nabsent++] = f;
        }
        if (nabsent) {
            char *names = uwl__func_list(absent, nabsent);
            uwl__set_err(err, "the .wit declares export%s the module does not export: %s",
                         nabsent > 1 ? "s" : "", names ? names : "?");
            free(names); free(absent);
            goto fail;
        }
        free(absent);
    }

    free(wasm_bytes);
    free(clean);
    return m;

fail:
    free(wasm_bytes);
    free(clean);
    uwl_free(m);
    return NULL;
}

/* ── calling ────────────────────────────────────────────────────────────── */

/* Allocate `len` bytes in WASM memory via cabi_realloc and copy `src` in.
   Returns the pointer, or -1 on failure. */
static int32_t uwl__alloc_in_wasm(uwl_module_t *m, const char *src, size_t len, char **err) {
    wasmtime_func_t cabi;
    if (!uwl__lookup_func(m, "cabi_realloc", 12, &cabi)) {
        uwl__set_err(err, "module does not export cabi_realloc (required for string params)");
        return -1;
    }
    wasmtime_val_t args[4];
    args[0].kind = WASMTIME_I32; args[0].of.i32 = 0;
    args[1].kind = WASMTIME_I32; args[1].of.i32 = 0;
    args[2].kind = WASMTIME_I32; args[2].of.i32 = 1;
    args[3].kind = WASMTIME_I32; args[3].of.i32 = (int32_t)len;
    wasmtime_val_t res; res.kind = WASMTIME_I32; res.of.i32 = 0;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *we = wasmtime_func_call(m->ctx, &cabi, args, 4, &res, 1, &trap);
    if (we) { uwl__err_from_wasmtime(err, "cabi_realloc", we); return -1; }
    if (trap) { uwl__err_from_trap(err, "cabi_realloc", trap); return -1; }
    int32_t ptr = res.of.i32;
    if (m->has_memory && len) {
        uint8_t *data = wasmtime_memory_data(m->ctx, &m->memory);
        size_t sz = wasmtime_memory_data_size(m->ctx, &m->memory);
        if (ptr < 0 || (size_t)ptr + len > sz) { uwl__set_err(err, "cabi_realloc returned OOB pointer"); return -1; }
        memcpy(data + ptr, src, len);
    }
    return ptr;
}

static int uwl__call_raw(uwl_module_t *m, const char *name,
                         const uwl_val_t *args, size_t nargs,
                         uwl_val_t *out, char **err);

int uwl_call(uwl_module_t *m, const char *name,
             const uwl_val_t *args, size_t nargs,
             uwl_val_t *out, char **err) {
    if (err) *err = NULL;
    *out = uwl_void();
    if (!m) { uwl__set_err(err, "null module"); return -1; }

    if (!m->has_wit) return uwl__call_raw(m, name, args, nargs, out, err);

    /* find the export by camelCase name */
    uwl_witfunc_t *f = NULL;
    for (size_t i = 0; i < m->wit.nexports; i++)
        if (!strcmp(m->wit.exports[i].camel, name)) { f = &m->wit.exports[i]; break; }
    if (!f) { uwl__set_err(err, "no export '%s' in WIT", name); return -1; }

    if (nargs != f->nparams) {
        uwl__set_err(err, "export '%s' expects %zu args, got %zu", name, f->nparams, nargs);
        return -1;
    }
    if (nargs && !args) { uwl__set_err(err, "export '%s': args is NULL", name); return -1; }

    /* SPEC 4.0.0 §4: each argument is the kind its WIT parameter declares,
       never coerced (a uwl_i32 for an f64 parameter is an error, not 2.0). */
    for (size_t i = 0; i < f->nparams; i++) {
        uwl_kind_t want = uwl__kind_of_wt(f->params[i]);
        if (args[i].kind != want) {
            uwl__set_err(err, "export '%s' parameter '%s' is %s, got %s (build it with %s)",
                         name, f->pnames[i], uwl__wt_name(f->params[i]),
                         uwl__kind_name(args[i].kind), uwl__kind_ctor(want));
            return -1;
        }
        if (want == UWL_STR && !args[i].str) {
            uwl__set_err(err, "export '%s' parameter '%s' is string, got a NULL string", name, f->pnames[i]);
            return -1;
        }
    }

    wasmtime_func_t func;
    if (!uwl__lookup_func(m, f->camel, strlen(f->camel), &func)) {
        uwl__set_err(err, "export '%s' not found in instance", f->camel);
        return -1;
    }

    /* Build WASM args (string → ptr,len). */
    size_t nwargs = 0;
    for (size_t i = 0; i < f->nparams; i++) nwargs += (f->params[i] == UWL_WT_STR) ? 2 : 1;
    wasmtime_val_t *wargs = nwargs ? (wasmtime_val_t *)malloc(nwargs * sizeof(wasmtime_val_t)) : NULL;
    size_t wi = 0;
    for (size_t i = 0; i < f->nparams; i++) {
        const uwl_val_t *a = &args[i];
        switch (f->params[i]) {
            case UWL_WT_STR: {
                int32_t ptr = uwl__alloc_in_wasm(m, a->str ? a->str : "", a->len, err);
                if (ptr < 0) { free(wargs); return -1; }
                wargs[wi].kind = WASMTIME_I32; wargs[wi].of.i32 = ptr; wi++;
                wargs[wi].kind = WASMTIME_I32; wargs[wi].of.i32 = (int32_t)a->len; wi++;
                break;
            }
            case UWL_WT_BOOL: wargs[wi].kind = WASMTIME_I32; wargs[wi].of.i32 = a->as.b ? 1 : 0; wi++; break;
            case UWL_WT_S64:  wargs[wi].kind = WASMTIME_I64; wargs[wi].of.i64 = a->as.i64; wi++; break;
            case UWL_WT_F32:  wargs[wi].kind = WASMTIME_F32; wargs[wi].of.f32 = a->as.f32; wi++; break;
            case UWL_WT_F64:  wargs[wi].kind = WASMTIME_F64; wargs[wi].of.f64 = a->as.f64; wi++; break;
            default:          wargs[wi].kind = WASMTIME_I32; wargs[wi].of.i32 = a->as.i32; wi++; break; /* s32 */
        }
    }

    int nres = (f->has_result) ? 1 : 0; /* string return = 1 i32 (retArea) */
    wasmtime_val_t res; res.kind = WASMTIME_I32; res.of.i32 = 0;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *we = wasmtime_func_call(m->ctx, &func, wargs, nwargs, nres ? &res : NULL, nres, &trap);
    free(wargs);
    if (we) { uwl__err_from_wasmtime(err, name, we); return -1; }
    if (trap) { uwl__err_from_trap(err, name, trap); return -1; }

    if (!f->has_result) { *out = uwl_void(); return 0; }

    if (f->result == UWL_WT_STR) {
        /* SPEC v3.0.0 callee-allocated return: res is an i32 ptr to [ptr,len]. */
        int32_t retArea = res.of.i32;
        if (!m->has_memory) { uwl__set_err(err, "string return but module has no memory"); return -1; }
        uint8_t *data = wasmtime_memory_data(m->ctx, &m->memory);
        size_t sz = wasmtime_memory_data_size(m->ctx, &m->memory);
        if (retArea < 0 || (size_t)retArea + 8 > sz) { uwl__set_err(err, "string return area OOB"); return -1; }
        int32_t sptr, slen;
        memcpy(&sptr, data + retArea, 4);
        memcpy(&slen, data + retArea + 4, 4);
        if (sptr < 0 || slen < 0 || (size_t)sptr + (size_t)slen > sz) { uwl__set_err(err, "string return bytes OOB"); return -1; }
        *out = uwl_strn((const char *)(data + sptr), (size_t)slen);

        /* Release the callee allocation via cabi_post_<camel> if present. */
        size_t pnlen = strlen("cabi_post_") + strlen(f->camel);
        char *post = (char *)malloc(pnlen + 1);
        snprintf(post, pnlen + 1, "cabi_post_%s", f->camel);
        wasmtime_func_t pf;
        if (uwl__lookup_func(m, post, strlen(post), &pf)) {
            wasmtime_val_t pa; pa.kind = WASMTIME_I32; pa.of.i32 = retArea;
            wasm_trap_t *pt = NULL;
            wasmtime_error_t *pe = wasmtime_func_call(m->ctx, &pf, &pa, 1, NULL, 0, &pt);
            if (pe) wasmtime_error_delete(pe);
            if (pt) wasm_trap_delete(pt);
        }
        free(post);
        return 0;
    }

    switch (f->result) {
        case UWL_WT_BOOL: *out = uwl_bool(res.of.i32 != 0); break;
        case UWL_WT_S64:  *out = uwl_i64(res.of.i64); break;
        case UWL_WT_F32:  *out = uwl_f32(res.of.f32); break;
        case UWL_WT_F64:  *out = uwl_f64(res.of.f64); break;
        default:          *out = uwl_i32(res.of.i32); break; /* s32 */
    }
    return 0;
}

/* Raw call (no WIT): args carry their own kinds; result decoded by valkind. */
static int uwl__call_raw(uwl_module_t *m, const char *name,
                         const uwl_val_t *args, size_t nargs,
                         uwl_val_t *out, char **err) {
    wasmtime_func_t func;
    if (!uwl__lookup_func(m, name, strlen(name), &func)) {
        uwl__set_err(err, "raw export '%s' not found", name);
        return -1;
    }
    wasmtime_val_t *wargs = nargs ? (wasmtime_val_t *)malloc(nargs * sizeof(wasmtime_val_t)) : NULL;
    for (size_t i = 0; i < nargs; i++) {
        switch (args[i].kind) {
            case UWL_I64:  wargs[i].kind = WASMTIME_I64; wargs[i].of.i64 = args[i].as.i64; break;
            case UWL_F32:  wargs[i].kind = WASMTIME_F32; wargs[i].of.f32 = args[i].as.f32; break;
            case UWL_F64:  wargs[i].kind = WASMTIME_F64; wargs[i].of.f64 = args[i].as.f64; break;
            case UWL_BOOL: wargs[i].kind = WASMTIME_I32; wargs[i].of.i32 = args[i].as.b ? 1 : 0; break;
            case UWL_STR:  free(wargs); uwl__set_err(err, "string args require a companion .wit file"); return -1;
            default:       wargs[i].kind = WASMTIME_I32; wargs[i].of.i32 = args[i].as.i32; break;
        }
    }
    /* Determine result count from the function type. */
    wasm_functype_t *ft = wasmtime_func_type(m->ctx, &func);
    size_t nres = wasm_functype_results(ft)->size;
    wasm_functype_delete(ft);
    wasmtime_val_t res; res.kind = WASMTIME_I32; res.of.i32 = 0;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *we = wasmtime_func_call(m->ctx, &func, wargs, nargs, nres ? &res : NULL, nres ? 1 : 0, &trap);
    free(wargs);
    if (we) { uwl__err_from_wasmtime(err, name, we); return -1; }
    if (trap) { uwl__err_from_trap(err, name, trap); return -1; }
    if (!nres) { *out = uwl_void(); return 0; }
    switch (res.kind) {
        case WASMTIME_I64: *out = uwl_i64(res.of.i64); break;
        case WASMTIME_F32: *out = uwl_f32(res.of.f32); break;
        case WASMTIME_F64: *out = uwl_f64(res.of.f64); break;
        default:           *out = uwl_i32(res.of.i32); break;
    }
    return 0;
}

/* ── lifecycle ──────────────────────────────────────────────────────────── */

void uwl_free(uwl_module_t *m) {
    if (!m) return;
    if (m->has_wit) uwl__wit_free(&m->wit);
    free(m->bindings);
    if (m->module) wasmtime_module_delete(m->module);
    if (m->store)  wasmtime_store_delete(m->store);
    if (m->engine) wasm_engine_delete(m->engine);
    free(m);
}

/* ── value helpers ──────────────────────────────────────────────────────── */

uwl_val_t uwl_strn(const char *s, size_t len) {
    uwl_val_t x; x.kind = UWL_STR; x.as.i64 = 0;
    x.str = (char *)malloc(len + 1);
    if (x.str) { if (len) memcpy(x.str, s, len); x.str[len] = '\0'; x.len = len; }
    else x.len = 0;
    return x;
}

uwl_val_t uwl_str(const char *s) { return uwl_strn(s ? s : "", s ? strlen(s) : 0); }

uwl_val_t uwl_err(const char *msg) {
    uwl_val_t x = uwl_str(msg ? msg : "(no message)");
    x.kind = UWL_ERR;
    return x;
}

void uwl_val_free(uwl_val_t *v) {
    if (!v) return;
    if (v->kind == UWL_STR || v->kind == UWL_ERR) free(v->str);
    *v = uwl_void();
}

void uwl_string_free(char *s) { free(s); }

/* ── convenience call layer ─────────────────────────────────────────────────
 * Typed variadic wrappers over uwl_call. The export's WIT signature drives how
 * each vararg is read, so callers pass native C values. Errors land in a
 * thread-local last-error string instead of an out-parameter. */

#if defined(_MSC_VER)
#  define UWL_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__)
#  define UWL_THREAD_LOCAL __thread
#else
#  define UWL_THREAD_LOCAL
#endif

static UWL_THREAD_LOCAL char *uwl__tls_err = NULL;

/* Replace the thread-local error, taking ownership of `msg` (may be NULL). */
static void uwl__tls_set(char *msg) {
    if (uwl__tls_err) free(uwl__tls_err);
    uwl__tls_err = msg;
}

const char *uwl_last_error(void) { return uwl__tls_err; }

/* Shared core: read varargs per the export's WIT params, call uwl_call, and
 * route the error to the thread-local. Returns 0 on success, -1 on failure
 * (with *out set to void). */
static int uwl__call_va(uwl_module_t *m, const char *name, uwl_val_t *out, va_list ap) {
    *out = uwl_void();
    if (!m) { uwl__tls_set(uwl__strdup("null module")); return -1; }
    if (!m->has_wit) {
        uwl__tls_set(uwl__strdup("convenience calls require a companion .wit; use uwl_call for raw modules"));
        return -1;
    }
    uwl_witfunc_t *f = NULL;
    for (size_t i = 0; i < m->wit.nexports; i++)
        if (!strcmp(m->wit.exports[i].camel, name)) { f = &m->wit.exports[i]; break; }
    if (!f) {
        char *e = NULL; uwl__set_err(&e, "no export '%s' in WIT", name);
        uwl__tls_set(e); return -1;
    }

    /* C varargs carry no types: each is read as the WIT parameter declares, so
       the caller must pass that C type. A NULL `const char *` for a string
       parameter is refused rather than sent as "". */
    uwl_val_t *args = f->nparams ? (uwl_val_t *)malloc(f->nparams * sizeof(uwl_val_t)) : NULL;
    if (f->nparams && !args) { uwl__tls_set(uwl__strdup("out of memory")); return -1; }
    size_t built = 0;
    char *err = NULL;
    for (size_t i = 0; i < f->nparams && !err; i++) {
        switch (f->params[i]) {
            case UWL_WT_STR: {
                const char *s = va_arg(ap, const char *);
                if (!s) uwl__set_err(&err, "export '%s' parameter '%s' is string, got NULL", name, f->pnames[i]);
                else args[built++] = uwl_str(s);
                break;
            }
            case UWL_WT_BOOL: args[built++] = uwl_bool(va_arg(ap, int)); break;
            case UWL_WT_S64:  args[built++] = uwl_i64(va_arg(ap, int64_t)); break;
            case UWL_WT_F32:  args[built++] = uwl_f32((float)va_arg(ap, double)); break;
            case UWL_WT_F64:  args[built++] = uwl_f64(va_arg(ap, double)); break;
            default:          args[built++] = uwl_i32(va_arg(ap, int32_t)); break; /* s32 */
        }
    }

    int rc = err ? -1 : uwl_call(m, name, args, f->nparams, out, &err);

    for (size_t i = 0; i < built; i++)
        if (args[i].kind == UWL_STR) uwl_val_free(&args[i]);
    free(args);

    uwl__tls_set(err); /* NULL on success clears the last error */
    return rc;
}

/* uwl__call_va, then require the result kind the accessor reads: a mismatch is
 * an error (sentinel + last error), never the union read as the wrong member
 * (workspace H29 / S8: call_i32 on an f64 export returned 0, call_bool on an
 * s32 export returned the integer). Returns 0 on success, -1 on failure. */
static int uwl__call_expect(uwl_module_t *m, const char *name, uwl_kind_t want,
                            uwl_val_t *out, va_list ap) {
    int rc = uwl__call_va(m, name, out, ap);
    if (rc == 0 && out->kind != want) {
        char *e = NULL;
        uwl__set_err(&e, "export '%s' returns %s, not %s", name,
                     uwl__kind_name(out->kind), uwl__kind_name(want));
        uwl__tls_set(e);
        uwl_val_free(out);
        return -1;
    }
    return rc;
}

int32_t uwl_call_i32(uwl_module_t *m, const char *name, ...) {
    va_list ap; va_start(ap, name);
    uwl_val_t out; int rc = uwl__call_expect(m, name, UWL_I32, &out, ap);
    va_end(ap);
    int32_t r = (rc == 0) ? uwl_as_i32(out) : 0;
    uwl_val_free(&out);
    return r;
}

int64_t uwl_call_i64(uwl_module_t *m, const char *name, ...) {
    va_list ap; va_start(ap, name);
    uwl_val_t out; int rc = uwl__call_expect(m, name, UWL_I64, &out, ap);
    va_end(ap);
    int64_t r = (rc == 0) ? uwl_as_i64(out) : 0;
    uwl_val_free(&out);
    return r;
}

float uwl_call_f32(uwl_module_t *m, const char *name, ...) {
    va_list ap; va_start(ap, name);
    uwl_val_t out; int rc = uwl__call_expect(m, name, UWL_F32, &out, ap);
    va_end(ap);
    float r = (rc == 0) ? uwl_as_f32(out) : 0.0f;
    uwl_val_free(&out);
    return r;
}

double uwl_call_f64(uwl_module_t *m, const char *name, ...) {
    va_list ap; va_start(ap, name);
    uwl_val_t out; int rc = uwl__call_expect(m, name, UWL_F64, &out, ap);
    va_end(ap);
    double r = (rc == 0) ? uwl_as_f64(out) : 0.0;
    uwl_val_free(&out);
    return r;
}

int uwl_call_bool(uwl_module_t *m, const char *name, ...) {
    va_list ap; va_start(ap, name);
    uwl_val_t out; int rc = uwl__call_expect(m, name, UWL_BOOL, &out, ap);
    va_end(ap);
    int r = (rc == 0) ? uwl_as_bool(out) : 0;
    uwl_val_free(&out);
    return r;
}

void uwl_call_void(uwl_module_t *m, const char *name, ...) {
    va_list ap; va_start(ap, name);
    uwl_val_t out; uwl__call_va(m, name, &out, ap);
    va_end(ap);
    uwl_val_free(&out);
}

char *uwl_call_str(uwl_module_t *m, const char *name, ...) {
    va_list ap; va_start(ap, name);
    uwl_val_t out; int rc = uwl__call_va(m, name, &out, ap);
    va_end(ap);
    if (rc != 0 || out.kind != UWL_STR) {
        if (rc == 0) uwl__tls_set(uwl__strdup("export did not return a string"));
        uwl_val_free(&out);
        return NULL;
    }
    char *s = out.str;          /* transfer ownership to the caller */
    out.str = NULL; out.kind = UWL_VOID;
    return s;
}

/* ── singleton ──────────────────────────────────────────────────────────── */

typedef struct {
    char  *name;
    uwl_host_fn_t fn;
    void  *userdata;
} uwl__cb_copy;

static uwl__cb_copy *uwl__copy_cbs(const uwl_host_callback_t *cbs, size_t n) {
    if (!n) return NULL;
    uwl__cb_copy *c = (uwl__cb_copy *)malloc(n * sizeof(uwl__cb_copy));
    for (size_t i = 0; i < n; i++) {
        c[i].name = uwl__strdup(cbs[i].name);
        c[i].fn = cbs[i].fn;
        c[i].userdata = cbs[i].userdata;
    }
    return c;
}

static void uwl__free_cbs(uwl__cb_copy *c, size_t n) {
    for (size_t i = 0; i < n; i++) free(c[i].name);
    free(c);
}

/* Rebuild a borrowed uwl_host_callback_t array from owned copies. */
static uwl_host_callback_t *uwl__view_cbs(uwl__cb_copy *c, size_t n) {
    if (!n) return NULL;
    uwl_host_callback_t *v = (uwl_host_callback_t *)malloc(n * sizeof(uwl_host_callback_t));
    for (size_t i = 0; i < n; i++) { v[i].name = c[i].name; v[i].fn = c[i].fn; v[i].userdata = c[i].userdata; }
    return v;
}

struct uwl_singleton {
    char         *path;
    uwl__cb_copy *cbs; size_t ncbs;
    uwl_module_t *module;
};

uwl_singleton_t *uwl_singleton_new(const char *wasm_path,
                                   const uwl_host_callback_t *callbacks, size_t ncallbacks) {
    uwl_singleton_t *s = (uwl_singleton_t *)calloc(1, sizeof(uwl_singleton_t));
    if (!s) return NULL;
    s->path = uwl__strdup(wasm_path);
    s->cbs = uwl__copy_cbs(callbacks, ncallbacks);
    s->ncbs = ncallbacks;
    return s;
}

uwl_module_t *uwl_singleton_get(uwl_singleton_t *s, char **err) {
    if (err) *err = NULL;
    if (!s) { uwl__set_err(err, "null singleton"); return NULL; }
    if (!s->module) {
        uwl_host_callback_t *view = uwl__view_cbs(s->cbs, s->ncbs);
        s->module = uwl_import(s->path, view, s->ncbs, err);
        free(view);
    }
    return s->module;
}

void uwl_singleton_free(uwl_singleton_t *s) {
    if (!s) return;
    if (s->module) uwl_free(s->module);
    uwl__free_cbs(s->cbs, s->ncbs);
    free(s->path);
    free(s);
}

/* ── pool ───────────────────────────────────────────────────────────────── */

struct uwl_pool {
    uwl_module_t **inst;
    int           *in_use;
    size_t         size;
};

uwl_pool_t *uwl_pool_new(const char *wasm_path,
                         const uwl_host_callback_t *callbacks, size_t ncallbacks,
                         size_t size, char **err) {
    if (err) *err = NULL;
    if (size == 0) size = 4;
    uwl_pool_t *p = (uwl_pool_t *)calloc(1, sizeof(uwl_pool_t));
    if (!p) { uwl__set_err(err, "out of memory"); return NULL; }
    p->size = size;
    p->inst = (uwl_module_t **)calloc(size, sizeof(uwl_module_t *));
    p->in_use = (int *)calloc(size, sizeof(int));
    for (size_t i = 0; i < size; i++) {
        p->inst[i] = uwl_import(wasm_path, callbacks, ncallbacks, err);
        if (!p->inst[i]) { uwl_pool_free(p); return NULL; }
    }
    return p;
}

uwl_module_t *uwl_pool_acquire(uwl_pool_t *p, char **err) {
    if (err) *err = NULL;
    if (!p) { uwl__set_err(err, "null pool"); return NULL; }
    for (size_t i = 0; i < p->size; i++) {
        if (!p->in_use[i]) { p->in_use[i] = 1; return p->inst[i]; }
    }
    uwl__set_err(err, "pool exhausted (size %zu)", p->size);
    return NULL;
}

void uwl_pool_release(uwl_pool_t *p, uwl_module_t *m) {
    if (!p) return;
    for (size_t i = 0; i < p->size; i++) {
        if (p->inst[i] == m) { p->in_use[i] = 0; return; }
    }
}

size_t uwl_pool_size(const uwl_pool_t *p) { return p ? p->size : 0; }

size_t uwl_pool_available(const uwl_pool_t *p) {
    if (!p) return 0;
    size_t n = 0;
    for (size_t i = 0; i < p->size; i++) if (!p->in_use[i]) n++;
    return n;
}

void uwl_pool_free(uwl_pool_t *p) {
    if (!p) return;
    for (size_t i = 0; i < p->size; i++) if (p->inst[i]) uwl_free(p->inst[i]);
    free(p->inst); free(p->in_use); free(p);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UWL_IMPLEMENTATION */
#endif /* UNIVERSAL_WASM_LOADER_H */
