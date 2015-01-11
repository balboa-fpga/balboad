#ifndef EIM_H_
#define EIM_H_

/* stub EIM header for eim.c */

enum eim_type {
    fpga_w_test0        = 0x0000,
    fpga_w_test1        = 0x0002,
    fpga_w_gpioa_dout   = 0x0010,
    fpga_w_gpioa_dir    = 0x0012,

    fpga_r_gpioa_din    = 0x1010,
};

#endif /* EIM_H_ */
