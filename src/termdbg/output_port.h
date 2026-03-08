#ifndef OUTPUT_PORT_H
#define OUTPUT_PORT_H

#include "common.h"

class OutputPort {
public:
  virtual void print(const char *text) { (void)text; }
  virtual void printf(const char *fmt, ...) { (void)fmt; }

  virtual ~OutputPort() = default;
};

#endif /* OUTPUT_PORT_H */
