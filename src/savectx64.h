#include "jmpbuf-offsets64.h"

struct savectx {
	void *regs[JB_SIZE];
};

int savectx(struct savectx *ctx);
void restorectx(struct savectx *ctx,int retval);

