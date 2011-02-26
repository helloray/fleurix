#include <param.h>
#include <x86.h>
#include <proc.h>
#include <proto.h>

#include <page.h>
#include <vm.h>

/*
 * the handler of a page fault raised on PTE_P is off.
 * */
void do_no_page(uint vaddr){
    struct vm *vm;
    struct vma *vp;
    struct pte *pte;
    struct page *pg;
    struct inode *ip;
    uint off, flag;
    char *buf;

    vm = &cu->p_vm;
    // if this page lies on the edge of user stack, grows the stack.
    if (vm->vm_stack.v_base - vaddr < PAGE) {
        vm->vm_stack.v_base -= PAGE;
        vm->vm_stack.v_size += PAGE;
        pg = pgalloc();
        pgattach(vm->vm_pgd, PG_ADDR(vaddr), pg, PTE_U|PTE_W|PTE_P);
        return;
    }
    // else
    vp = find_vma(vaddr);
    if (vp==NULL) {
        // TODO: segmentation fault.
        return;
    }
    // demand zero
    if (vp->v_flag & VMA_ZERO) {
        pg = pgalloc();
        pgattach(vm->vm_pgd, PG_ADDR(vaddr), pg, PTE_U|PTE_W|PTE_P);
        memset(PG_ADDR(vaddr), 0, PAGE);
        return;
    }
    // demand file
    if (vp->v_flag & VMA_MMAP) {
        pg = pgalloc();
        pte = pgattach(vm->vm_pgd, PG_ADDR(vaddr), pg, PTE_U|PTE_W|PTE_P);
        // fill this new-allocated page
        // hint: vaddr is *ALWAYS* greater than or equal with vp->v_base
        buf = PG_ADDR(vaddr);
        off = buf - vp->v_base + vp->v_ioff;
        ip = iget(vp->v_dev, vp->v_ino);
        if (ip==NULL) {
            panic("do_no_page(): bad ino.");
        }
        readi(ip, buf, off, PAGE);
        iput(ip);
        pte->pt_flag &= ~(vp->v_flag&VMA_RDONLY? 0:PTE_W);
        flmmu();
        return;
    }
    // segmentation fault
    panic("do_no_page()");
}

/* 
 * the handler of the read-only protection. here is the 'copy' action
 * of the implemtion of copy-on-write.
 * If this one is the last one who did the write, just mark the page as
 * write-able.
 * else allocate one page and associate inside the page table. 
 * */
void do_wp_page(uint vaddr){
    struct vma *vp;
    struct pte *pte;
    struct page *pg;
    char *old_page, *new_page;

    vp = find_vma(vaddr);
    if (vp->v_flag & VMA_RDONLY) {
        printf("vaddr: %x\n", vaddr);
        panic("do_wp_page(): rdonly vma.");
        return;
    }
    if (vp->v_flag & VMA_PRIVATE) {
        pte = find_pte(vaddr, 0);
        pg = pgfind(pte->pt_off);
        if (pg->pg_count > 1) {
            pg->pg_count--; //decrease the reference count of the old page.
            old_page = (char*)(pg->pg_num * PAGE);
            new_page = (char*)kmalloc(PAGE);
            memcpy(new_page, old_page, PAGE);
            pte->pt_off = PPN(new_page);
            pte->pt_flag |= PTE_W;
            flmmu();
        }
        else if (pg->pg_count==1) {
            pte->pt_flag |= PTE_W;
            flmmu();
        }
    }
}

/*
 * the common handler of all page faults, as a dispatcher.
 * */
void do_pgfault(struct trap *tf){
    uint addr;

    asm volatile("movl %%cr2, %0":"=a"(addr));
    // invalid page
    if ((tf->err_code & PFE_P)==0) {
        do_no_page(addr);
        return;
    }
    // write procted page
    if (tf->err_code & PFE_W) {
        do_wp_page(addr);
        return;
    }
    // TODO: raise a signal here, hence segmention fault.
    if (tf->err_code & PFE_U) {
    }
    debug_regs(tf);
    panic("~");
}

