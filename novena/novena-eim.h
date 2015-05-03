#ifndef NOVENA_EIM_H_
#define NOVENA_EIM_H_

// XXX these values from novena-fpga-drivers/novena-fpga.c, is it correct?
#define EIM_CS0_BASE 0x08040000
#define EIM_CS0_SIZE (64 * 1024 * 1024)

int prep_eim(void);

#endif /* NOVENA_EIM_H_ */
