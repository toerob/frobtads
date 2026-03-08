#ifndef EVAL_PORT_H
#define EVAL_PORT_H

#include "common.h"

#include "vmglob.h"

#include <cstddef>

class EvalPort {
public:
  virtual int eval_expr(VMG_ char *res, size_t res_size, const char *expr,
                        int flags, int *is_lval, int *is_openable, int, int,
                        int) {
    (void)res;
    (void)res_size;
    (void)expr;
    (void)flags;
    (void)is_lval;
    (void)is_openable;
    return 1;
  }

  virtual void enum_locals(VMG_ void (*cbfunc)(void *, const char *, size_t),
                           void *cbctx, int level) {
    (void)cbfunc;
    (void)cbctx;
    (void)level;
  }

  virtual ~EvalPort() = default;
};

#endif /* EVAL_PORT_H */
