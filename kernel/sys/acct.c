/* =============================================================================
 *  AGOS - CPU and task accounting (sys/acct.c)
 * ========================================================================== */
#include "acct.h"
#include "heap.h"
#include "lib.h"
#include "pit.h"

#define NAME_MAX_LEN 24
#define NEST_MAX     6
#define SAMPLE_US    1000000u

static struct {
    char     name[NAME_MAX_LEN];
    bool     alive;
    uint32_t acc_us;                    /* time charged since boot (wraps)     */
    uint32_t acc_at_sample;
    uint32_t permille;
} tasks[ACCT_MAX_TASKS];

static uint32_t idle_acc, idle_at_sample, idle_start;
static bool     in_idle;
static uint32_t last_sample;
static uint32_t cpu_pm;

static int      stack[NEST_MAX];
static uint8_t  owner_stack[NEST_MAX];
static int      depth;
static uint32_t seg_start;

static uint8_t  hist[ACCT_HISTORY];
static unsigned hist_len;

void acct_init(void)
{
    memset(tasks, 0, sizeof(tasks));
    kstrlcpy(tasks[0].name, "kernel_task", NAME_MAX_LEN);
    tasks[0].alive = true;
    idle_acc = idle_at_sample = 0;
    in_idle = false;
    depth = 0;
    hist_len = 0;
    cpu_pm = 0;
    last_sample = pit_clock_us();
}

int acct_register(const char *name)
{
    for (int i = 1; i < ACCT_MAX_TASKS; i++) {
        if (!tasks[i].alive) {
            memset(&tasks[i], 0, sizeof(tasks[i]));
            kstrlcpy(tasks[i].name, name, NAME_MAX_LEN);
            tasks[i].alive = true;
            return i;
        }
    }
    return -1;
}

void acct_unregister(int id)
{
    if (id > 0 && id < ACCT_MAX_TASKS)
        tasks[id].alive = false;
}

bool acct_alive(int id)
{
    return id >= 0 && id < ACCT_MAX_TASKS && tasks[id].alive;
}

const char *acct_name(int id)
{
    return (id >= 0 && id < ACCT_MAX_TASKS) ? tasks[id].name : "?";
}

void acct_idle_begin(void)
{
    idle_start = pit_clock_us();
    in_idle = true;
}

void acct_idle_end(void)
{
    if (in_idle) {
        idle_acc += pit_clock_us() - idle_start;
        in_idle = false;
    }
}

void acct_task_enter(int id)
{
    uint32_t now = pit_clock_us();

    if (depth > 0)
        tasks[stack[depth - 1]].acc_us += now - seg_start;
    if (depth < NEST_MAX) {
        stack[depth] = (id > 0 && id < ACCT_MAX_TASKS) ? id : 0;
        owner_stack[depth] = heap_get_owner();
        depth++;
        heap_set_owner((uint8_t)stack[depth - 1]);
    }
    seg_start = now;
}

void acct_task_leave(void)
{
    uint32_t now = pit_clock_us();

    if (depth == 0)
        return;
    tasks[stack[depth - 1]].acc_us += now - seg_start;
    depth--;
    heap_set_owner(owner_stack[depth]);
    seg_start = now;
}

bool acct_tick(void)
{
    uint32_t now = pit_clock_us();
    uint32_t elapsed = now - last_sample;

    if (elapsed < SAMPLE_US)
        return false;

    if (in_idle) {                       /* count the idle stretch so far       */
        idle_acc += now - idle_start;
        idle_start = now;
    }
    if (depth > 0) {                     /* ...and the running task's           */
        tasks[stack[depth - 1]].acc_us += now - seg_start;
        seg_start = now;
    }

    uint32_t idle_d = idle_acc - idle_at_sample;
    uint32_t busy   = elapsed > idle_d ? elapsed - idle_d : 0;
    uint32_t unit   = elapsed / 1000u;   /* >= 1000, so no division by zero     */
    uint32_t sum    = 0;

    cpu_pm = busy / unit;
    if (cpu_pm > 1000)
        cpu_pm = 1000;

    for (int i = 1; i < ACCT_MAX_TASKS; i++) {
        uint32_t d = tasks[i].acc_us - tasks[i].acc_at_sample;

        tasks[i].acc_at_sample = tasks[i].acc_us;
        tasks[i].permille = d / unit;
        sum += d;
    }
    {
        uint32_t d = tasks[0].acc_us - tasks[0].acc_at_sample;

        tasks[0].acc_at_sample = tasks[0].acc_us;
        (void)d;                         /* task 0 is the remainder instead     */
        tasks[0].permille = (busy > sum ? busy - sum : 0) / unit;
    }

    idle_at_sample = idle_acc;
    last_sample = now;

    uint8_t pct = (uint8_t)((cpu_pm + 5) / 10);
    if (hist_len < ACCT_HISTORY) {
        hist[hist_len++] = pct;
    } else {
        memmove(hist, hist + 1, ACCT_HISTORY - 1);
        hist[ACCT_HISTORY - 1] = pct;
    }
    return true;
}

uint32_t acct_cpu_permille(void)
{
    return cpu_pm;
}

uint32_t acct_task_permille(int id)
{
    return (id >= 0 && id < ACCT_MAX_TASKS) ? tasks[id].permille : 0;
}

unsigned acct_history(uint8_t *out)
{
    memcpy(out, hist, hist_len);
    return hist_len;
}
