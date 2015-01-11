#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdint.h>

#include "gpio.h"
#include "eim.h"

#define EIM_BASE (0x08040000)
#define EIM_DOUT (0x0010)
#define EIM_DIR (0x0012)
#define EIM_DIN (0x1010)

static int   *mem_32 = 0;
static short *mem_16 = 0;
static char  *mem_8  = 0;
static int   *prev_mem_range = 0;

uint8_t cached_dout = 0;
uint8_t cached_dir = 0;

static int read_kernel_memory(long offset, int virtualized, int size) {
	int result;
	static int mem_fd;

	int *mem_range = (int *)(offset & ~0xFFFF);
	if( mem_range != prev_mem_range ) {
		prev_mem_range = mem_range;

		if(mem_32)
			munmap(mem_32, 0xFFFF);
		if(mem_fd)
			close(mem_fd);

		if(virtualized) {
			mem_fd = open("/dev/kmem", O_RDWR);
			if( mem_fd < 0 ) {
				perror("Unable to open /dev/kmem");
				mem_fd = 0;
				return -1;
			}
		}
		else {
			mem_fd = open("/dev/mem", O_RDWR);
			if( mem_fd < 0 ) {
				perror("Unable to open /dev/mem");
				mem_fd = 0;
				return -1;
			}
		}

		mem_32 = mmap(0, 0xffff, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, offset&~0xFFFF);
		if( (void *)-1 == mem_32 ) {
			perror("Unable to mmap file");

			if( -1 == close(mem_fd) )
				perror("Also couldn't close file");

			mem_fd=0;
			return -1;
		}
		mem_16 = (short *)mem_32;
		mem_8  = (char  *)mem_32;
	}

	int scaled_offset = (offset-(offset&~0xFFFF));
	if(size==1)
		result = mem_8[scaled_offset/sizeof(char)];
	else if(size==2)
		result = mem_16[scaled_offset/sizeof(short)];
	else
		result = mem_32[scaled_offset/sizeof(long)];

	return result;
}

static int write_kernel_memory(long offset, long value, int virtualized, int size) {
	int old_value = read_kernel_memory(offset, virtualized, size);
	int scaled_offset = (offset-(offset&~0xFFFF));
	if(size==1)
		mem_8[scaled_offset/sizeof(char)]   = value;
	else if(size==2)
		mem_16[scaled_offset/sizeof(short)] = value;
	else
		mem_32[scaled_offset/sizeof(long)]  = value;
	return old_value;
}


int prep_eim(void) {
	int i;
	// set up pads to be mapped to EIM
	for( i = 0; i < 16; i++ ) {
		write_kernel_memory( 0x20e0114 + i*4, 0x0, 0, 4 );  // mux mapping
		write_kernel_memory( 0x20e0428 + i*4, 0xb0b1, 0, 4 ); // pad strength config'd for a 100MHz rate 
	}

	// mux mapping
	write_kernel_memory( 0x20e046c - 0x314, 0x0, 0, 4 ); // BCLK
	write_kernel_memory( 0x20e040c - 0x314, 0x0, 0, 4 ); // CS0
	write_kernel_memory( 0x20e0410 - 0x314, 0x0, 0, 4 ); // CS1
	write_kernel_memory( 0x20e0414 - 0x314, 0x0, 0, 4 ); // OE
	write_kernel_memory( 0x20e0418 - 0x314, 0x0, 0, 4 ); // RW
	write_kernel_memory( 0x20e041c - 0x314, 0x0, 0, 4 ); // LBA
	write_kernel_memory( 0x20e0468 - 0x314, 0x0, 0, 4 ); // WAIT
	write_kernel_memory( 0x20e0408 - 0x314, 0x0, 0, 4 ); // A16
	write_kernel_memory( 0x20e0404 - 0x314, 0x0, 0, 4 ); // A17
	write_kernel_memory( 0x20e0400 - 0x314, 0x0, 0, 4 ); // A18

	// pad strength
	write_kernel_memory( 0x20e046c, 0xb0b1, 0, 4 ); // BCLK
	write_kernel_memory( 0x20e040c, 0xb0b1, 0, 4 ); // CS0
	write_kernel_memory( 0x20e0410, 0xb0b1, 0, 4 ); // CS1
	write_kernel_memory( 0x20e0414, 0xb0b1, 0, 4 ); // OE
	write_kernel_memory( 0x20e0418, 0xb0b1, 0, 4 ); // RW
	write_kernel_memory( 0x20e041c, 0xb0b1, 0, 4 ); // LBA
	write_kernel_memory( 0x20e0468, 0xb0b1, 0, 4 ); // WAIT
	write_kernel_memory( 0x20e0408, 0xb0b1, 0, 4 ); // A16
	write_kernel_memory( 0x20e0404, 0xb0b1, 0, 4 ); // A17
	write_kernel_memory( 0x20e0400, 0xb0b1, 0, 4 ); // A18

	write_kernel_memory( 0x020c4080, 0xcf3, 0, 4 ); // ungate eim slow clocks

	// EIM_CS0GCR1   
	// 0101 0  001 1   001    0   001 11  00  0  000  1    0   1   1   1   0   0   1
	// PSZ  WP GBC AUS CSREC  SP  DSZ BCS BCD WC BL   CREP CRE RFL WFL MUM SRD SWR CSEN
	//
	// PSZ = 0101  256 words page size
	// WP = 0      (not protected)
	// GBC = 001   min 1 cycles between chip select changes
	// AUS = 0     address shifted according to port size
	// CSREC = 001 min 1 cycles between CS, OE, WE signals
	// SP = 0      no supervisor protect (user mode access allowed)
	// DSZ = 001   16-bit port resides on DATA[15:0]
	// BCS = 11    3 clock delay for burst generation
	// BCD = 00    divide EIM clock by 1 for burst clock
	// WC = 0      specify write bust according to BL
	// BL = 000    4 words wrap burst length
	// CREP = 1    non-PSRAM, set to 1
	// CRE = 0     CRE is disabled
	// RFL = 1     fixed latency reads (don't monitor WAIT)
	// WFL = 1     fixed latency writes (don't monitor WAIT)
	// MUM = 1     multiplexed mode enabled
	// SRD = 0     no synch reads
	// SWR = 0     no synch writes
	// CSEN = 1    chip select is enabled

	// 0101 0111 1111    0001 1100  0000  1011   1   0   0   1
	// 0x5  7    F        1   C     0     B    9

	// 0101 0001 1001    0001 1100  0000  1011   1001
	// 5     1    9       1    c     0     B      9

	write_kernel_memory( 0x21b8000, 0x5191C0B9, 0, 4 );

	// EIM_CS0GCR2   
	//  MUX16_BYP_GRANT = 1
	//  ADH = 1 (1 cycles)
	//  0x1001
	write_kernel_memory( 0x21b8004, 0x1001, 0, 4 );


	// EIM_CS0RCR1   
	// 00 000101 0 000   0   000   0 000 0 000 0 000 0 000
	//    RWSC     RADVA RAL RADVN   OEA   OEN   RCSA  RCSN
	// RWSC 000101    5 cycles for reads to happen
	//
	// 0000 0111 0000   0011   0000 0000 0000 0000
	//  0    7     0     3      0  0    0    0
	// 0000 0101 0000   0000   0 000 0 000 0 000 0 000
	//  write_kernel_memory( 0x21b8008, 0x05000000, 0, 4 );
	write_kernel_memory( 0x21b8008, 0x0A024000, 0, 4 );
	// EIM_CS0RCR2  
	// 0000 0000 0   000 00 00 0 010  0 001 
	//           APR PAT    RL   RBEA   RBEN
	// APR = 0   mandatory because MUM = 1
	// PAT = XXX because APR = 0
	// RL = 00   because async mode
	// RBEA = 000  these match RCSA/RCSN from previous field
	// RBEN = 000
	// 0000 0000 0000 0000 0000  0000
	write_kernel_memory( 0x21b800c, 0x00000000, 0, 4 );

	// EIM_CS0WCR1
	// 0   0    000100 000   000   000  000  010 000 000  000
	// WAL WBED WWSC   WADVA WADVN WBEA WBEN WEA WEN WCSA WCSN
	// WAL = 0       use WADVN
	// WBED = 0      allow BE during write
	// WWSC = 000100 4 write wait states
	// WADVA = 000   same as RADVA
	// WADVN = 000   this sets WE length to 1 (this value +1)
	// WBEA = 000    same as RBEA
	// WBEN = 000    same as RBEN
	// WEA = 010     2 cycles between beginning of access and WE assertion
	// WEN = 000     1 cycles to end of WE assertion
	// WCSA = 000    cycles to CS assertion
	// WCSN = 000    cycles to CS negation
	// 1000 0111 1110 0001 0001  0100 0101 0001
	// 8     7    E    1    1     4    5    1
	// 0000 0111 0000 0100 0000  1000 0000 0000
	// 0      7    0   4    0     8    0     0
	// 0000 0100 0000 0000 0000  0100 0000 0000
	//  0    4    0    0     0    4     0    0

	write_kernel_memory( 0x21b8010, 0x09080800, 0, 4 );

	// EIM_WCR
	// BCM = 1   free-run BCLK
	// GBCD = 0  don't divide the burst clock
	write_kernel_memory( 0x21b8090, 0x1, 0, 4 );

	// EIM_WIAR 
	// ACLK_EN = 1
	write_kernel_memory( 0x21b8094, 0x10, 0, 4 );

	return 0;
}

uint16_t *eim_get(enum eim_type type) {
	static uint16_t *mem = NULL;
	static int fd;

	if (mem)
		return ((uint16_t *) (((uint8_t *)mem)+type));

	prep_eim();

	fd = open("/dev/mem", O_RDWR);
	if (fd == -1) {
		perror("Couldn't open /dev/mem");
		return NULL;
	}

	mem = mmap(NULL, 0xffff, PROT_READ | PROT_WRITE, MAP_SHARED, fd, EIM_BASE);
	if (mem == ((uint16_t *)-1)) {
		perror("Couldn't mmap EIM region");
		return NULL;
	}

	return eim_get(type);
}

int eim_set_direction(int gpio, int is_output) {
	uint16_t *mem = eim_get(fpga_w_gpioa_dir);
	if (!mem)
		return -1;
	gpio &= ~GPIO_IS_EIM;
	if (is_output)
		cached_dir |=  (1<<gpio);
	else
		// Clear direction
		cached_dir &= ~(1<<gpio);

	*mem = cached_dir;
	return 0;
}


int eim_set_value(int gpio, int value) {
	uint16_t *mem = eim_get(fpga_w_gpioa_dout);
	if (!mem)
		return -1;
	gpio &= ~GPIO_IS_EIM;
	if (value)
		cached_dout |= (1<<gpio);
	else
		cached_dout &= ~(1<<gpio);
	*mem = cached_dout;
	return 0;
}

int eim_get_value(int gpio) {
	uint16_t *mem = eim_get(fpga_r_gpioa_din);
	if (!mem)
		return -1;
	gpio &= ~GPIO_IS_EIM;
	return (*mem >> gpio)&1;
}

