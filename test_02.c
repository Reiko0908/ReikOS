#include "include/cpu.h"
#include "include/mm.h"
#include "include/mm64.h"
#include "include/libmem.h"
#include <stdio.h>
#include <stdlib.h>
#include "print_debug.h"

int main()
{
    /* =========================
     * 1. INIT RAM + SWAP
     * ========================= */
    struct memphy_struct mram;
    struct memphy_struct mswp[PAGING_MAX_MMSWP];

    init_memphy(&mram, 1 << 14, 1);

    init_memphy(&mswp[0], 1 << 14, 0);
    init_memphy(&mswp[1], 1 << 14, 0);
    init_memphy(&mswp[2], 1 << 14, 0);
    init_memphy(&mswp[3], 1 << 14, 0);

    /* =========================
     * 2. INIT KERNEL (os)
     * ========================= */
    struct krnl_t os;

    os.mram = &mram;

    os.mswp = malloc(sizeof(struct memphy_struct *) * PAGING_MAX_MMSWP);
    for (int i = 0; i < PAGING_MAX_MMSWP; i++)
        os.mswp[i] = &mswp[i];

    os.active_mswp = &mswp[0];
    os.active_mswp_id = 0;

    // /* PAGE TABLE */

    // os.krnl_pgd = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
    // os.krnl_p4d = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
    // os.krnl_pud = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
    // os.krnl_pmd = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
    // os.krnl_pt = malloc(PAGING64_MAX_PGN * sizeof(addr_t));

    // for (int i = 0; i < PAGING64_MAX_PGN; i++)
    // {
    //     os.krnl_pgd[i] = (addr_t)&os.krnl_p4d;
    //     os.krnl_p4d[i] = (addr_t)&os.krnl_pud;
    //     os.krnl_pud[i] = (addr_t)&os.krnl_pmd;
    //     os.krnl_pmd[i] = (addr_t)&os.krnl_pt;
    //     os.krnl_pt[i] = 0;
    // }

    /* =========================
     * 3. INIT PROCESS
     * ========================= */
    struct pcb_t *proc = malloc(sizeof(struct pcb_t));
    proc->pid = 1;
    proc->krnl = &os;

    /* =========================
     * 4. INIT MM
     * ========================= */
    os.mm = malloc(sizeof(struct mm_struct));
    init_mm(os.mm, proc);

    /* =========================
     * 5. RUN
     * ========================= */
    set_current_proc(proc);

    struct inst_t ins;

    /* WRITE */
    ins.opcode = ALLOC;
    ins.arg_0 = 32;
    ins.arg_1 = 0;
    run(proc, &ins);

    ins.opcode = ALLOC;
    ins.arg_0 = 32;
    ins.arg_1 = 1;
    run(proc, &ins);
    for (int i = 0; i < 16; i++)
    {
        ins.opcode = WRITE;
        ins.arg_0 = i + 1; // data
        ins.arg_1 = 0;     // region
        ins.arg_2 = i * 4; // offset

        run(proc, &ins);
    }

    /* READ */
    for (int i = 0; i < 16; i++)
    {
        ins.opcode = READ;
        ins.arg_0 = 0;
        ins.arg_1 = i * 4;
        ins.arg_2 = 0;
        run(proc, &ins);

        printf("Read[%d] = %lu - ", i, ins.arg_2);
    }
    printf("\n");
    print_kernel_info(&os);

    return 0;
}