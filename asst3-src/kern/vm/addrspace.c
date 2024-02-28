/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	// initialised as needed in as_define_region
	as->regions = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas = as_create();
	if (!newas) return ENOMEM;

	// copying over regions to the new address space
	struct as_regions *region;
	for (region = old->regions; region != NULL; region = region->next) {
		int ret = as_define_region(newas, region->base, region->size, region->permissions | READ, region->permissions | WRITE, region->permissions | EXECUTE);
		if (ret) {
			as_destroy(newas);
			return ret;
		}
	}

	// copy page table entries
	for (region = newas->regions; region != NULL; region = region->next) {
		int ret = hpt_copy(region, old, newas);
		if (ret) {
			as_destroy(newas);
			return ret;
		}
	}

	*ret = newas;
	as_activate();
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	struct as_regions *prev = NULL;
	for (struct as_regions *region = as->regions; region != NULL; region = region->next) {
		hpt_free(as, region->base, region->size);
		if (prev) kfree(prev);
		prev = region;		
	}
	if (prev) kfree(prev);
	kfree(as);
	as_deactivate();
}

void
as_activate(void)
{
	struct addrspace *as;
	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}
	
	// Invalidate all entries in the TLB.
	int spl = splhigh();

	for (int i = 0; i < NUM_TLB; i++)
	{
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	// Re-enable interrupts on the current processor.
	splx(spl); 
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
	as_activate();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permissions should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */

	if (as == NULL) {
        return EINVAL;
    }

    // Align the region to the page size
    memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;
    memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	 // Check if it is a valid region
    if ((vaddr + memsize) > MIPS_KSEG0) {
        return EFAULT;
    }

	// Check for overlapping regions
    struct as_regions *curr = as->regions;
    while (curr != NULL) {
        if ((vaddr <= curr->base + curr->size) && (vaddr + memsize) >= curr->base) {
            return EINVAL;
        }
        curr = curr->next;
    }

	// Initialize the new region
    struct as_regions *new_region = kmalloc(sizeof(struct as_regions));
    if (new_region == NULL) {
        return ENOMEM;
    }

	// Set base, size, permissions
	new_region->base = vaddr;
    new_region->size = memsize;
    new_region->read_only_change = false;
    new_region->permissions = 0;

	if (readable) new_region->permissions |= READ;
    if (writeable) new_region->permissions |= WRITE;
    if (executable) new_region->permissions |= EXECUTE;

	new_region->next = as->regions;
	as->regions = new_region;

	return 0; 
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	if (as == NULL) {
        return EINVAL;
    }

    for (struct as_regions *region = as->regions; region != NULL; region = region->next) {
		// Check if the region is read-only (READ permissions but not WRITE permission)
		if ((region->permissions & READ) && !(region->permissions & WRITE)) {
            // Temporarily make the region writeable
            region->permissions |= WRITE;
            // Set the read_only_change flag to true
            region->read_only_change = true;

			set_page_write_permissions(as, region->base, region->size);
        }
    }

	
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	if (as == NULL) return EINVAL;

    for (struct as_regions *region = as->regions; region != NULL; region = region->next) {
		// Check if the read_only_change flag is true
		if (region->read_only_change) {
            // Restore the original write permissions (remove the WRITE permission)
            region->permissions &= ~WRITE;
            // Set the read_only_change flag back to false
            region->read_only_change = false;

			reset_page_write_permissions(as, region->base, region->size);
        }
    }

	as_activate();	
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	if (as == NULL || stackptr == NULL) {
		return EINVAL;
	}
	// The size of the stack
	size_t stack_size = VM_STACKPAGES * PAGE_SIZE;

	// Calculate the base address of the stack
	vaddr_t stack_base = USERSTACK - stack_size;

	int define_stack = as_define_region(as, stack_base, stack_size, 1, 1, 0);
	if (define_stack) {
		return 1;
	}

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

