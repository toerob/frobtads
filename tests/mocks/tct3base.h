#ifndef TCT3BASE_H
#define TCT3BASE_H

// Test-only shim: enough for dapdebugui.cc globals-scope compilation.

#include <cstddef>

#include "mock_vm.h"

// Symbol types (minimal subset)
#ifndef TC_SYM_OBJ
#define TC_SYM_OBJ 1
#endif

class CTcSymbol {
public:
  int get_type() const { return TC_SYM_OBJ; }
  const char *get_sym() const { return ""; }
  std::size_t get_sym_len() const { return 0; }
  vm_obj_id_t get_val_obj() const { return VM_INVALID_OBJ; }
};

class CTcSymTab {
public:
  using EnumFn = void (*)(void *, CTcSymbol *);
  void enum_entries(EnumFn /*cb*/, void * /*ctx*/) {}
};

class CTcPrs {
public:
  CTcSymTab *get_global_symtab() { return nullptr; }
};

// Exposed as a global in the VM; we stub it for compilation.
extern CTcPrs *G_prs;

#endif /* TCT3BASE_H */
