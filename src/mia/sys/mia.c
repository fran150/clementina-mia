#include "sys/mia.h"

#include <stdio.h>
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"

#include "hardware/gpio_mapping.h"
#include "hardware/pio_mapping.h"
#include "hardware/clocks.h"
#include "mem/mem.h"
#include "sys.pio.h"
#include "rom/kernel_data.h"

#define TARGET_HZ 2000

uint32_t kernel_index = 0;
uint16_t kernel_target_address = 0x4000;

// Used to compare against the (CS | R/W | addr) value. In case of reads
// the value will be 1 | 0 | 5 bits address.
#define CASE_READ(addr) (addr & 0x1F)
// Used to compare against the (CS | R/W | addr) value. In case of writes
// the value will be 1 | 1 | 5 bits address
#define CASE_WRITE(addr) (0x20 | (addr & 0x1F))

__attribute__((optimize("O1"))) static void __no_inline_not_in_flash_func(act_loop)(void)
{
    // In here we bypass the usual SDK calls as needed for performance.
    while (true)
    {
        // If PIO send and action in the RX FIFO
        if (!(MIA_ACT_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + MIA_ACT_SM))))
        {
            // Get the pins data (CS | R/W | 5 Address bits | 8 Data bits)
            uint32_t rw_addr_data = MIA_ACT_PIO->rxf[MIA_ACT_SM];
            
            // Parse data bits
            //uint32_t data = rw_addr_data & 0xFF;

            // Remove the data bits leaving only the CS, R/W and address pins
            switch (rw_addr_data >> 8)
            {
                // After reading the byte that contains the value for the destination address of
                // the kernel
                case CASE_READ(0xFFF6):
                    // If there are are values still on the kernel we set the new value
                    // and increment the destination address to the next byte
                    if (kernel_index < kernel_data_size) {
                        REGS(0xFFF1) = kernel_data[kernel_index++];
                        REGSW(0xFFF3) += 1;
                    }

                    // If we reached the end of the kernel we replace the instruction at 
                    // $FFF0 (beginning of fast loader) with JMP to kernel target address.
                    if (kernel_index < kernel_data_size) {
                        REGS(0xFFF0) = 0x4C;
                        REGS(0xFFF1) = kernel_target_address & 0xFF;
                        REGS(0xFFF2) = kernel_target_address >> 8;
                    }
                break;
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

    // TODO: Review set PIO speed
   // configure_phi2_frequency(&config, TARGET_HZ);

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
    pio_sm_put(MIA_WRITE_PIO, MIA_WRITE_SM, (uintptr_t)regs >> 5);
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
        regs,                              // dst (this will get updated by the address DMA)
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
    pio_sm_put(MIA_READ_PIO, MIA_READ_SM, (uintptr_t)regs >> 5);
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
        regs,                            // src
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
    
    // Start PIO program
    pio_sm_set_enabled(MIA_ACT_PIO, MIA_ACT_SM, true);
    // Start act loop in core 1
    multicore_launch_core1(act_loop);
}

void fast_loader_init(void) {
    for (int i = 0xFFDF; i < 0xFFFF; i++) {
        REGS(i) = 0xAA;
    }

    // // // Self-modifying fast load
    // REGS(0xFFF0) = 0xA9;                            // FFF0  A9 00     LDA #<byte> ; The MIA will respond with the byte of the kernel
    // REGS(0xFFF1) = kernel_data[kernel_index++];
    
    // REGS(0xFFF2) = 0x8D;                            // FFF2  8D 00 00  STA $0000 ; The target address to write the kernel
    // REGS(0xFFF3) = kernel_target_address & 0xFF;
    // REGS(0xFFF4) = kernel_target_address >> 8;

    // REGS(0xFFF5) = 0x80;                            // FFF5  80 F9     BRA $FFF0
    // REGS(0xFFF6) = 0xF9;
    // REGS(0xFFF7) = 0x80;                            // FFF7  80 FE     BRA $FFF7
    // REGS(0xFFF8) = 0xFE;

    // // Reset vector initialization to 0xFFF0
    // REGS(0xFFFC) = 0xF0;
    // REGS(0xFFFD) = 0xFF;

    // regs[28] = 0xF0;
    // regs[29] = 0xFF;
    // regs[30] = 0xF0;
    // regs[31] = 0xFF;

    for (int i = 0xFFDF; i < 0xFFFF; i++) {
       printf("Value of %i: %i \n", i, REGS(i));
    }

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
    assert(!((uintptr_t)regs & 0x1F));

    // Adjustments for GPIO performance. Important!
    for (int i = MIA_PIN_BASE; i < MIA_PIN_BASE + 15; i++)
    {
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