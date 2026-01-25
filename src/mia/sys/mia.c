#include "sys/mia.h"

#include <stdio.h>
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"

#include "cfg/cfg.h"
#include "cmds/cmds.h"
#include "hardware/gpio_mapping.h"
#include "hardware/pio_mapping.h"
#include "hardware/clocks.h"
#include "mem/indexes.h"
#include "mem/regs.h"
#include "rom/kernel_data.h"

#include "sys.pio.h"

#define TARGET_HZ 2000

// Configuration for the kernel loader
uint32_t kernel_index = 0;                      // Index pointing to the next byte to be read from the kernel
uint16_t kernel_target_address = 0x4000;        // Target address in where kernel is being written

static bool can_update_kernel_pointer = false;  // Flag to allow updating the kernel pointer only after the data is read at least once.

// State of the MIA chip, it starts in loader mode.
// After the kernel has been loaded in ram it switches to normal operation
static enum mia_states {
    mia_state_loader = 0,
    mia_state_normal = 1
} volatile mia_state = mia_state_loader;

// Used to compare against the (CS | R/W | addr) value. In case of reads
// the value will be 1 | 0 | 5 bits address.
#define CASE_READ(addr) (addr & 0x1F)
// Used to compare against the (CS | R/W | addr) value. In case of writes
// the value will be 1 | 1 | 5 bits address
#define CASE_WRITE(addr) (0x20 | (addr & 0x1F))

// MIA will notify the action loop of all register writes.
// Only every fourth register (0, 4, 8, ...) is watched for read access. This additional read address to be watched
// is varied based on the state of the MIA.
static void mia_set_watch_address(uint32_t addr) {
    pio_sm_put(MIA_ACT_PIO, MIA_ACT_SM, addr & 0x1F);
}

// This is the main loop user to process the actions after reads or writes to MIA registers
__attribute__((optimize("O1"))) static void __no_inline_not_in_flash_func(act_loop)(void) {
    // In here we bypass the usual SDK calls as needed for performance.
    while (true) {
        // If PIO send and action in the RX FIFO
        if (!(MIA_ACT_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + MIA_ACT_SM)))) {
            // Get the pins data (CS | R/W | 5 Address bits | 8 Data bits)
            uint32_t rw_addr_data = MIA_ACT_PIO->rxf[MIA_ACT_SM];
            
            // Parse data bits
            uint32_t data = rw_addr_data & 0xFF;
            
            // Remove the data bits leaving only the CS, R/W and address pins
            uint32_t address = rw_addr_data >> 8;

            switch (mia_state) {
                case mia_state_loader:                    
                    switch (address) {
                        // Reading the kernel value enables updating
                        case CASE_READ(0xFFE1):
                            can_update_kernel_pointer = true;
                            break;

                        // Writing to the address used to pull the kernel value updates the pointer
                        // to the next value. This can only be done if the address was read in the first place
                        case CASE_WRITE(0xFFF1):
                            if (!can_update_kernel_pointer) {
                                break;
                            }

                            // If there are are values still on the kernel we set the new value
                            // and increment the destination address to the next byte
                            if (kernel_index < kernel_data_size) {
                                REGS(0xFFE1) = kernel_data[kernel_index++];
                                REGSW(0xFFE3) += 1;
                            }

                            // If we reached the end of the kernel we replace the BRA $FFF0 instruction with BRA #$00
                            // so it won't branch. $FFF7 contains a jump to the first address of kernel.
                            if (kernel_index >= kernel_data_size) {
                                REGS(0xFFE9) = 0x00;
                            }

                            // Disable pointer update so consecutive writes can update it only once. 
                            // It must be read to enable further reads
                            can_update_kernel_pointer = false;

                            break;                            
                    }     

                    break;

                case mia_state_normal:
                    switch (address) {                        
                        case CASE_READ(0xFFE0):
                            // After reading port A value, step the index and set the new value
                            mia_regs->idxa_port = step_index_and_read(mia_regs->idxa_selector);
                            break;
                            
                        case CASE_WRITE(0xFFE0):
                            // After writing to port A, copy the value to the actual memory and step the index
                            write_index_and_step(mia_regs->idxa_selector, data);
                            // Read the new value into the port
                            mia_regs->idxa_port = read_index(mia_regs->idxa_selector);
                            break;

                        case CASE_WRITE(0xFFE1):
                            // When changing the selected index A, read the memory where the index is pointing and set it on port A
                            mia_regs->idxa_port = read_index(data);
                            break;

                        case CASE_WRITE(0xFFE2):
                            // Writing to the CFG selector gets the current value of that config into the register
                            mia_regs->cfg_port = get_cfg(data);
                            break;

                        case CASE_WRITE(0xFFE3):
                            // Writing to the dataport updates the config value
                            set_cfg(mia_regs->cfg_selector, data);
                            break;


                        case CASE_READ(0xFFE4):
                            // After reading port B value, step the index and set the new value
                            mia_regs->idxb_port = step_index_and_read(mia_regs->idxb_selector);
                            break;
                            
                        case CASE_WRITE(0xFFE4):
                            // After writing to port B, copy the value to the actual memory and step the index
                            write_index_and_step(mia_regs->idxb_selector, data);
                            // Read the new value into the port
                            mia_regs->idxb_port = read_index(mia_regs->idxb_selector);
                            break;

                        case CASE_WRITE(0xFFE5):
                            // When changing the selected index B, read the memory where the index is pointing and set it on port B
                            mia_regs->idxb_port = read_index(data);
                            break;

                        case CASE_WRITE(0xFFE9):
                            execute_command(mia_regs->cmd_trigger, mia_regs->cmd_param1, mia_regs->cmd_param2, mia_regs->cmd_param3);
                            break;
                    }
            }
        }
    }
}

// Updates the PIO configuration to run at a speed to generate the PHI2 signal
// at the specified frequency (min 2 Khz)
void configure_phi2_frequency(pio_sm_config *config, float target_hz) {
    // The PIO needs to run 32 times faster than the PHI2 wave
    float pio_freq = target_hz * 32.0f;
    float system_freq = (float)clock_get_hz(clk_sys);

    // Calculate the divider
    float div = system_freq / pio_freq;

    // Safety check: PIO divider must be between 1.0 and 65536.0
    if (div < 1.0f) div = 1.0f;

    // Apply the divider to the config
    sm_config_set_clkdiv(config, div);
}

// Initializes the PIO program that monitors the CS and R/W enable pins and adjusts
// the databus pins directions accordingly.
static void mia_cs_rwb_pio_init(void)
{
    // Add and configure the PIO program
    uint offset = pio_add_program(MIA_CS_RWB_PIO, &mia_cs_rwb_program);
    pio_sm_config config = mia_cs_rwb_program_get_default_config(offset);

    // Input pins configuration
    sm_config_set_in_pins(&config, MIA_PIN_BASE);
    sm_config_set_in_shift(&config, false, false, 0);
    sm_config_set_in_pin_count(&config, 2);

    // Output pins configuration
    sm_config_set_out_pins(&config, MIA_DATA_PIN_BASE, 8);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_out_pin_count(&config, 8);

    // PIO SM reset and configuration
    pio_sm_init(MIA_CS_RWB_PIO, MIA_CS_RWB_SM, offset, &config);

    // Sets Y record in the PIO to zero
    pio_sm_exec_wait_blocking(MIA_READ_PIO, MIA_READ_SM, pio_encode_set(pio_y, 0));

    // Start PIO program
    pio_sm_set_enabled(MIA_CS_RWB_PIO, MIA_CS_RWB_SM, true);
}

// Initializes the PIO program that handles 6502 writes to the MIA
// It also generates PHI2 clock signal on the side set.
static void mia_write_pio_init(void)
{
    // Add and configure the PIO program
    uint offset = pio_add_program(MIA_WRITE_PIO, &mia_write_program);
    pio_sm_config config = mia_write_program_get_default_config(offset);

    // Input pins configuration
    sm_config_set_in_pins(&config, MIA_PIN_BASE);
    sm_config_set_in_shift(&config, false, false, 0);

    // Output pins configuration
    sm_config_set_out_pins(&config, MIA_DATA_PIN_BASE, 8);

    // PHI2 generation with sideset configuration (generates a square pulse with 50% duty cycle and 32 PIO cycles width)
    sm_config_set_sideset_pins(&config, CPU_PHI2_PIN);
    pio_gpio_init(MIA_WRITE_PIO, CPU_PHI2_PIN);
    pio_sm_set_consecutive_pindirs(MIA_WRITE_PIO, MIA_WRITE_SM, CPU_PHI2_PIN, 1, true);

    // PIO SM reset and configuration
    pio_sm_init(MIA_WRITE_PIO, MIA_WRITE_SM, offset, &config);

    // Puts the pointer of the regs variable in the TX FIFO, pulls it and moves
    // it to Y register in the PIO. The first 5 bits are removed as later it will create
    // the offset to a specific register by combining this value with the one in the 6502 address bus
    pio_sm_put(MIA_WRITE_PIO, MIA_WRITE_SM, (uintptr_t)mia_regs >> 5);
    pio_sm_exec_wait_blocking(MIA_WRITE_PIO, MIA_WRITE_SM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(MIA_WRITE_PIO, MIA_WRITE_SM, pio_encode_mov(pio_y, pio_osr));

    // Start PIO program
    pio_sm_set_enabled(MIA_WRITE_PIO, MIA_WRITE_SM, true);

    // Prepare DMA for address and data bus processing
    int addr_chan = dma_claim_unused_channel(true);
    int data_chan = dma_claim_unused_channel(true);


    // Configures DMA channel to move data from PIO for input
    dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    channel_config_set_high_priority(&data_dma, true);
    channel_config_set_dreq(&data_dma, pio_get_dreq(MIA_WRITE_PIO, MIA_WRITE_SM, false));
    channel_config_set_read_increment(&data_dma, false);    
    channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);

    // When this DMA completes it enables the address DMA to start the cycle again and process
    // the next write
    channel_config_set_chain_to(&data_dma, addr_chan);

    // The data DMA moves the value from the receive FIFO to the address of the register specified by
    // the address DMA completing the write
    dma_channel_configure(
        data_chan,
        &data_dma,
        mia_regs,                          // dst (this will get updated by the address DMA)
        &MIA_WRITE_PIO->rxf[MIA_WRITE_SM], // src
        1,
        false);


    // Configures DMA channel to move address from PIO into the data DMA config
    dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    channel_config_set_high_priority(&addr_dma, true);
    channel_config_set_dreq(&addr_dma, pio_get_dreq(MIA_WRITE_PIO, MIA_WRITE_SM, false));
    channel_config_set_read_increment(&addr_dma, false);
    
    // When this DMA completes it enables the data DMA
    channel_config_set_chain_to(&addr_dma, data_chan);

    // The address DMA moves the data from the RX FIFO (PIO puts Y register base address + 5 bits of 6502 address bus for offset on it)
    // to the write address of the data DMA.
    dma_channel_configure(
        addr_chan,
        &addr_dma,
        &dma_channel_hw_addr(data_chan)->write_addr, // dst
        &MIA_WRITE_PIO->rxf[MIA_WRITE_SM],           // src
        1,
        true);
}

// Initializes the PIO program that handles 6502 reads to the MIA
static void mia_read_pio_init(void)
{
    // Add and configure the PIO program
    uint offset = pio_add_program(MIA_READ_PIO, &mia_read_program);
    pio_sm_config config = mia_read_program_get_default_config(offset);

    // Input pins configuration
    sm_config_set_in_pins(&config, MIA_ADDR_PIN_BASE);
    sm_config_set_in_shift(&config, false, true, 5);

    // Output pins configuration
    sm_config_set_out_pins(&config, MIA_DATA_PIN_BASE, 8);
    sm_config_set_out_shift(&config, true, true, 8);
    // Init databus pins
    for (int i = MIA_DATA_PIN_BASE; i < MIA_DATA_PIN_BASE + 8; i++) {
        pio_gpio_init(MIA_READ_PIO, i);
    }

    // PIO SM reset and configuration
    pio_sm_init(MIA_READ_PIO, MIA_READ_SM, offset, &config);

    // Puts the pointer of the regs variable in the TX FIFO, pulls it and moves
    // it to Y register in the PIO. The first 5 bits are removed as later it will create
    // the offset to a specific register by combining this value with the one in the 6502 address bus
    pio_sm_put(MIA_READ_PIO, MIA_READ_SM, (uintptr_t)mia_regs >> 5);
    pio_sm_exec_wait_blocking(MIA_READ_PIO, MIA_READ_SM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(MIA_READ_PIO, MIA_READ_SM, pio_encode_mov(pio_y, pio_osr));

    // Start PIO program
    pio_sm_set_enabled(MIA_READ_PIO, MIA_READ_SM, true);

    // Prepare DMA for address and data bus processing
    int addr_chan = dma_claim_unused_channel(true);
    int data_chan = dma_claim_unused_channel(true);


    // Configures DMA channel to move data to PIO for output
    dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    channel_config_set_high_priority(&data_dma, true);
    channel_config_set_dreq(&data_dma, pio_get_dreq(MIA_READ_PIO, MIA_READ_SM, true));
    channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);

    // When this DMA completes it enables the address DMA to start the cycle again and process
    // the next read
    channel_config_set_chain_to(&data_dma, addr_chan);

    // The data DMA moves the value from the address of the register specified by
    // the address DMA into the transmit FIFO. Then the PIO program pulls the value and writes it to 
    // GPIOs completing the read
    dma_channel_configure(
        data_chan,
        &data_dma,
        &MIA_READ_PIO->txf[MIA_READ_SM], // dst
        mia_regs,                        // src
        1,
        false);

        
    // Configures DMA channel to move address from PIO into the data DMA config
    dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    channel_config_set_high_priority(&addr_dma, true);
    channel_config_set_dreq(&addr_dma, pio_get_dreq(MIA_READ_PIO, MIA_READ_SM, false));
    channel_config_set_read_increment(&addr_dma, false);

    // When this DMA completes it enables the data DMA
    channel_config_set_chain_to(&addr_dma, data_chan);

    // The address DMA moves the data from the RX FIFO (PIO puts Y register base address + 5 bits of 6502 address bus for offset on it)
    // to the read address of the data DMA.
    dma_channel_configure(
        addr_chan,
        &addr_dma,
        &dma_channel_hw_addr(data_chan)->read_addr, // dst
        &MIA_READ_PIO->rxf[MIA_READ_SM],            // src (this will get updated by the address DMA)
        1,
        true);
}

// Initializes the PIO program that feeds the main action loop with events
// This is used for MIA to take action based on reads or writes to the registers
// MIA will notify the action loop of all register writes.
// Only every fourth register (0, 4, 8, ...) is watched for read access.
// Aditional address to be watched can be specified using the mia_set_watch_address
// Initially address 0xFFE1 (kernel data port) is watched
static void mia_act_pio_init(void)
{
    // Add and configure the PIO program
    uint offset = pio_add_program(MIA_ACT_PIO, &mia_action_program);
    pio_sm_config config = mia_action_program_get_default_config(offset);

    // Configure input pins to the base pin. This will read all pins starting
    // from the MIA_PIN_BASE
    sm_config_set_in_pins(&config, MIA_PIN_BASE);
    sm_config_set_in_shift(&config, true, true, 32);

    // PIO SM reset and configuration
    pio_sm_init(MIA_ACT_PIO, MIA_ACT_SM, offset, &config);

    // Sets the initial watch address for the act_loop function.
    mia_set_watch_address(0xFFE1);
    
    // Start PIO program
    pio_sm_set_enabled(MIA_ACT_PIO, MIA_ACT_SM, true);
    // Start act loop in core 1
    multicore_launch_core1(act_loop);
}

void fast_loader_init(void) {
    // // Self-modifying fast load
    REGS(0xFFE0) = 0xA9;                            // FFE0:  A9 xx     LDA #xx ; The MIA will respond with the byte of the kernel
    REGS(0xFFE1) = kernel_data[kernel_index++];
    
    REGS(0xFFE2) = 0x8D;                            // FFE2:  8D xx xx  STA $xxxx ; The target address to write the kernel
    REGS(0xFFE3) = kernel_target_address & 0xFF;
    REGS(0xFFE4) = kernel_target_address >> 8;

    REGS(0xFFE5) = 0x8D;                            // FFE5:  8D F1 FF  STA $FFF1 ; Gets the next instruction in the kernel data port
    REGS(0xFFE6) = 0xF1;
    REGS(0xFFE7) = 0xFF;

    REGS(0xFFE8) = 0x80;                            // FFE5:  80 F6     BRA $F6 ; Loops to 0xFFE0 for the next instruction
    REGS(0xFFE9) = 0xF6;

    REGS(0xFFEA) = 0x4C;                            // FFE7:  4C xx xx  JMP $<kernel base>
    REGS(0xFFEB) = kernel_target_address & 0xFF;
    REGS(0xFFEC) = kernel_target_address >> 8;

    // Reset vector initialization to 0xFFF0
    REGS(0xFFFC) = 0xF0;
    REGS(0xFFFD) = 0xFF;
}

// Initializes the MIA
void mia_init(void)
{
    // Configures the IRQ pin as output for the MIA to drive
    // Initializes the pin to high
    gpio_init(CPU_IRQB_PIN);
    gpio_put(CPU_IRQB_PIN, true);
    gpio_set_dir(CPU_IRQB_PIN, true);

    // Safety check for compiler alignment
    assert(!((uintptr_t)mia_regs & 0x1F));

    // Adjustments for GPIO performance. Important!
    for (int i = MIA_PIN_BASE; i < MIA_PIN_BASE + 15; i++) {
        // Hands control of pins to PIO
        pio_gpio_init(pio0, i); // Any pio
        
        // Disables pull up / down resistors
        gpio_set_pulls(i, false, false);
        
        // Disables Schmitt trigger for performance 
        gpio_set_input_hysteresis_enabled(i, false);

        // Bypass synchronizer
        hw_set_bits(&pio0->input_sync_bypass, 1u << i);
        hw_set_bits(&pio1->input_sync_bypass, 1u << i);
        hw_set_bits(&pio2->input_sync_bypass, 1u << i);
    }

    // Give core 1 high priority in bus arbitration
    bus_ctrl_hw->priority |= BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    // It might be tempting to raise DMA_R/W here, but this causes problems with mia_write_buf

    // Setup the fast loader program for the 6502 in the first 9 recors of the MIA and set the
    // reset vector pointing to the start of this program.
    fast_loader_init();

    // Initialize all PIO programs
    mia_cs_rwb_pio_init();
    mia_write_pio_init();
    mia_read_pio_init();
    mia_act_pio_init();
}