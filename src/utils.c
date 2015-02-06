#define USE_RINTERNALS 1
#include <Rversion.h>
#include <Rinternals.h>
#include <R_ext/Parse.h>

/* string encoding handling */
#if (R_VERSION < R_Version(2,8,0)) || (defined DISABLE_ENCODING)
#define mkRChar(X) mkChar(X)
#else
#define USE_ENCODING 1
/* in Rserv.c */
extern cetype_t string_encoding;  /* default is native */
#define mkRChar(X) mkCharCE((X), string_encoding)
#endif

extern Rboolean R_Visible;

/* this is really convoluted - we want to be guaranteed to not leave the call
   on one hand, but on the other hand R_ToplevelExec() removes the context
   which also removes the traceback. So the trick is to use R_ExecWithCleanup()
   to add another layer where we stash the traceback before R_ToplevelExec()
   blows it away. It woudl be really just one extra line in R sources, but
   what can you do ... */

typedef struct rs_eval {
    SEXP what, rho, last, traceback;
    int exp;
} rs_eval_t;

static SEXP Rserve_eval_do(void *arg) {
    rs_eval_t *e = (rs_eval_t*) arg;
    SEXP what = e->what, rho = e->rho, x;
    int i, n;

    if (TYPEOF(what) == EXPRSXP) {
        n = LENGTH(what);
        for (i = 0; i < n; i++) {
            e->exp = i;
            x = eval(VECTOR_ELT(what, i), rho);
            if (i == n - 1) {
                R_PreserveObject(x);
                e->last = x;
            }
            if (R_Visible)
                PrintValue(x);
        }
    } else {
        e->exp = -1;
        x = eval(what, rho);
        R_PreserveObject(x);
        if (R_Visible)
            PrintValue(x);
        e->last = x;
    }
    return R_NilValue;
}

/* it's really stupid becasue R has R_GetTraceback() but we have to
   jump through eval() just because it's hidden so we can't access it ... */
static SEXP R_GetTraceback(int skip) {
    SEXP d_int = install(".Internal"), tb = install("traceback"), sSkip = PROTECT(ScalarInteger(skip));
    SEXP what = PROTECT(lang2(d_int, lang2(tb, sSkip)));
    SEXP res = eval(what, R_GlobalEnv);
    UNPROTECT(2);
    return res;    
}

static void Rserve_eval_cleanup(void *arg) {
    rs_eval_t *e = (rs_eval_t*) arg;
    SEXP tb = R_GetTraceback(0);
    if (tb && tb != R_NilValue)
        R_PreserveObject((e->traceback = tb));
}

static void Rserve_eval_(void *arg) {
    rs_eval_t *e = (rs_eval_t*) arg;
    R_ExecWithCleanup(Rserve_eval_do, arg, Rserve_eval_cleanup, arg);
}

SEXP Rserve_eval(SEXP what, SEXP rho, SEXP retLast, SEXP retExp) {
    int need_last = asInteger(retLast), exp_value = asInteger(retExp);
    rs_eval_t e = { what, rho, 0, 0, 0 };
    if (!R_ToplevelExec(Rserve_eval_, &e)) {
        SEXP res = PROTECT(mkNamed(VECSXP, (const char*[]) { "error", "traceback", "expression", "" }));
        SET_VECTOR_ELT(res, 1, e.traceback ? e.traceback : R_NilValue);
        const char *errmsg = R_curErrorBuf();
        SET_VECTOR_ELT(res, 0, errmsg ? mkString(errmsg) : R_NilValue);
        if (exp_value)
            SET_VECTOR_ELT(res, 2, (e.exp == -1) ? what : VECTOR_ELT(what, e.exp));
        else
            SET_VECTOR_ELT(res, 2, ScalarInteger(e.exp < 0 ? NA_INTEGER : (e.exp + 1)));
        setAttrib(res, R_ClassSymbol, mkString("Rserve-eval-error"));
        UNPROTECT(1);
        return res;
    }

    if (need_last) {
        if (e.last) {
            SEXP res = PROTECT(mkNamed(VECSXP, (const char*[]) { "result", "" }));
            SET_VECTOR_ELT(res, 0, e.last);
            R_ReleaseObject(e.last);
            UNPROTECT(1);
            return res;
        }
        return allocVector(VECSXP, 0);
    }
    return ScalarLogical(TRUE);
}