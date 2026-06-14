#include "cmds.h"

#include "pico/multicore.h"

#include "etc/err.h"
#include "etc/status.h"
#include "audio/audio.h"
#include "input/input.h"
#include "irq/irq.h"
#include "mem/dma.h"
#include "mem/indexes.h"
#include "mem/regs.h"
#include "sys/exec.h"
#include "video/video.h"

#define UNUSED(x) (void)(x)

// Command handler function type
typedef void (*command_t)(uint8_t param[]);

// List of commands
static command_t commands[256];

/**************************************************************************************************
 * Commands implementations
 **************************************************************************************************/

// Generic empty command that does nothing.
// This is by default the command for all command ids
void command_empty(uint8_t param[]) {  
    UNUSED(param);  
}

// Resets the index in window A
void command_reset_index_a(uint8_t param[]) {
    UNUSED(param);

    uint8_t id = mia_regs->idxa_selector;
    reset_index(id);
}

// Resets the index in window B
void command_reset_index_b(uint8_t param[]) {
    UNUSED(param);

    uint8_t id = mia_regs->idxb_selector;
    reset_index(id);
}

// Resets the index specified in param 0
void command_reset_index(uint8_t param[]) {
    reset_index(param[0]);
}

// Sets the specified index default to current address
void command_set_index_default_to_current_addr(uint8_t param[]) {
    uint32_t current = index_get_current_addr(param[0]);
    index_set_default_addr(param[0], current);
}

// Set the specified index limit to current address
void command_set_index_limit_to_current_addr(uint8_t param[]) {
    uint32_t current = index_get_current_addr(param[0]);
    index_set_limit_addr(param[0], current);
}

// Resets all indexes
void command_reset_all_index(uint8_t param[]) {
    UNUSED(param);

    for (uint16_t i = 0; i < 256; i++) {
        reset_index((uint8_t)i);
    }
}

void command_peek_from_index_to_a(uint8_t param[]) {
    uint8_t index_id = param[0];
    mia_regs->idxa_port = index_read(index_id);
}

void command_peek_from_index_to_b(uint8_t param[]) {
    uint8_t index_id = param[0];
    mia_regs->idxb_port = index_read(index_id);
}


// Triggers a dma transfer of the specified number of bytes from the source index to the 
// destination index. Index are not moved.
void command_copy_indexes(uint8_t param[]) {
    uint8_t from = param[0];    // index id that points to the source addess
    uint8_t to = param[1];      // index id that points to the destination address
    uint8_t count = param[2];   // number of bytes to move

    // Trigger the pico DMA transfer
    mia_dma_transfer_init(idx[from].current_addr, idx[to].current_addr, count);
}

void command_video_force_full_refresh(uint8_t param[]) {
    UNUSED(param);

    mia_video_force_full_refresh();
}

void command_video_set_mode(uint8_t param[]) {
    mia_video_set_mode(param[0]);
}

void command_exec_pause(uint8_t param[]) {
    UNUSED(param);

    mia_exec_pause();
}

void command_input_set_mode(uint8_t param[]) {
    if (!mia_input_set_mode((mia_input_mode_t)param[0])) {
        error_push(ERROR_INPUT_MODE_UNAVAILABLE);
    }
}

void command_input_set_probe(uint8_t param[]) {
    if (!mia_input_set_probe(param[0], param[1])) {
        error_push(ERROR_INPUT_PROBE_INVALID);
    }
}

void command_audio_enable(uint8_t param[]) {
    UNUSED(param);

    mia_audio_enable();
}

void command_audio_stop(uint8_t param[]) {
    UNUSED(param);

    mia_audio_stop();
}

void command_audio_reset(uint8_t param[]) {
    UNUSED(param);

    mia_audio_reset();
}

/**************************************************************************************************
 * Init and crosscore messaging handling
 **************************************************************************************************/

// Handle interrupt from multicore FIFO. Messages come in the form: CMD_ID | P1 | P2 | P3
void on_fifo_irq() {
    // Sets the busy status flag in the MIA
    mia_status_set_flag(MIA_STAT_CMD_RUNNING);

    // Keep pulling all messages
    while (multicore_fifo_rvalid()) {
        // Pop the message
        uint32_t msg = multicore_fifo_pop_blocking();
        
        // Unpack the message extracting command id and parameters
        uint8_t id = (msg >> 24) & 0xFF;
        uint8_t p1 = (msg >> 16) & 0xFF;
        uint8_t p2 = (msg >> 8)  & 0xFF;
        uint8_t p3 = msg         & 0xFF;

        if (commands[id] == command_empty) {
            error_push(ERROR_CMD_UNKNOWN);
        } else {
            commands[id]((uint8_t[3]) { p1, p2, p3 });
        }
    }

    // Clears the FIFO interrupt
    multicore_fifo_clear_irq();

    // Clears the busy status flag in the MIA
    mia_status_clear_flag(MIA_STAT_CMD_RUNNING);

    // Notify the 6502 that command execution finished. The bit latches and only
    // raises the line if IRQ_COMMAND is enabled in IRQ_MASK. Asynchronous
    // commands (e.g. the DMA copy) re-raise this again on actual completion.
    mia_irq_set_flag(IRQ_COMMAND);
}

// Inits the command system. This prepares the lookup table of commands
// and sets up the code for multicore communication
void mia_command_init() {
    // Set all commands to empty by default
    for (int i = 0; i < 256; i++) {
        commands[i] = command_empty;
    }   

    // Creates the command lookup table
    commands[0x00] = command_reset_index_a;
    commands[0x01] = command_reset_index_b;
    commands[0x02] = command_reset_index;
    commands[0x03] = command_set_index_default_to_current_addr;
    commands[0x04] = command_set_index_limit_to_current_addr;
    commands[0x05] = command_reset_all_index;
    commands[0x06] = command_peek_from_index_to_a;
    commands[0x07] = command_peek_from_index_to_b;

    commands[0x10] = command_copy_indexes;

    commands[0x30] = command_exec_pause;

    commands[0x42] = command_video_force_full_refresh;
    commands[0x43] = command_video_set_mode;

    commands[0x50] = command_input_set_mode;
    commands[0x51] = command_input_set_probe;

    commands[MIA_CMD_AUDIO_ENABLE] = command_audio_enable;
    commands[MIA_CMD_AUDIO_STOP] = command_audio_stop;
    commands[MIA_CMD_AUDIO_RESET] = command_audio_reset;

    // Clear the FIFO IRQ
    multicore_fifo_clear_irq();

    // Configure the handler for intercore communication
    irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(0), on_fifo_irq);
    irq_set_enabled(SIO_FIFO_IRQ_NUM(0), true);    
}
