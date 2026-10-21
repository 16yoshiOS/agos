/* =============================================================================
 *  AGOS - CPU and task accounting (sys/acct.h, sys/acct.c)
 *  AGOS has no preemptive processes yet, so a "task" is an application whose
 *  event handlers and drawing code run inside acct_task_enter()/_leave().
 *  CPU usage is real: idle time is measured around HLT, task time between
 *  enter/leave, both with the PIT's microsecond clock; everything else is
 *  charged to task 0 ("kernel_task": window server, drivers, compositor).
 * ========================================================================== */
#ifndef AGOS_ACCT_H
#define AGOS_ACCT_H

#include <stdbool.h>
#include <stdint.h>

#define ACCT_MAX_TASKS   12             /* must be <= HEAP_MAX_OWNERS          */
#define ACCT_KERNEL_TASK 0
#define ACCT_HISTORY     60             /* seconds of CPU history kept         */

void acct_init(void);

/* Register an application; returns its task id (1..), or -1 if the table is
 * full. Task 0 always exists. */
int  acct_register(const char *name);
void acct_unregister(int id);
bool acct_alive(int id);
const char *acct_name(int id);

/* Wrap the CPU-idle HLT in the main loop. */
void acct_idle_begin(void);
void acct_idle_end(void);

/* Attribute the code between enter and leave to `id` (nestable). The heap
 * owner follows the current task, so allocations are charged to it too. */
void acct_task_enter(int id);
void acct_task_leave(void);

/* Call regularly from the main loop; samples once per second. Returns true
 * when a new sample was taken. */
bool acct_tick(void);

/* Latest one-second sample, in tenths of a percent (0..1000). */
uint32_t acct_cpu_permille(void);
uint32_t acct_task_permille(int id);

/* History of total CPU load, one byte (0..100 %) per second, oldest first.
 * Returns the number of valid samples copied (<= ACCT_HISTORY). */
unsigned acct_history(uint8_t *out);

#endif /* AGOS_ACCT_H */
