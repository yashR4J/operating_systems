#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <thread.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <proc.h>

/* Place your page table functions here */
uint32_t HPT_SIZE = 0;

/*
    Initializes the hash page table (HPT) by calculating its size,
    allocating memory for it, and initializing each entry.
*/
void hpt_init(void)
{
    // Get the total size of physical memory
    paddr_t ram_size = ram_getsize();

    // Calculate the size of the hash page table and allocate memory for it
    HPT_SIZE = (ram_size / PAGE_SIZE) * 2;
    hpt = (struct hpt_entry *)kmalloc(sizeof(struct hpt_entry) * HPT_SIZE);

    // Initialize each hash page table entry
    for (uint32_t i = 0; i < HPT_SIZE; i++)
    {
        hpt[i].pid = 0;     // The process ID for this entry
        hpt[i].entrylo = 0; // The entrylo register for this entry
        hpt[i].entryhi = 0; // The entryhi register for this entry
        hpt[i].next = NULL; // Pointer to next entry in case of collision
    }
}

/*
    Computes the hash index for the given address space and virtual address.
*/
uint32_t hpt_hash(struct addrspace *as, vaddr_t address)
{
    // Make sure the address is aligned to a page boundary.
    KASSERT((address & PAGE_FRAME) == address);

    // Calculate the hash index by combining the address space pointer and
    // the high-order bits of the address. The resulting value is modulo the
    // hash table size.
    uint32_t index = (((uint32_t)as) ^ (address >> 12)) % HPT_SIZE;
    return index;
}

/*
    Zeroes out the memory of the allocated physical page(s)
    given a physical address and the number of pages to zero.
*/
void zero_pad(paddr_t paddr, unsigned npages)
{
    KASSERT(paddr && npages);
    bzero((void *)paddr, npages * PAGE_SIZE);
}

/*
    Adds a new page table entry to the hash page table for the given address space,
    virtual address, and write permissions. Optionally writes the new entry to the TLB.
*/
int hpt_add(struct addrspace *as, vaddr_t vaddr, int permissions, bool write_to_tlb)
{
    // Ensure `as` and `vaddr` are not NULL
    KASSERT(as && vaddr);
    vaddr &= PAGE_FRAME;
    vaddr_t paddr = alloc_kpages(1);

    if (!paddr)
        return ENOMEM; // out of frames
    KASSERT(paddr % PAGE_SIZE == 0);

    uint32_t index = hpt_hash(as, vaddr);
    uint32_t curr_index = index;

    struct hpt_entry *entry = &hpt[curr_index];

    // find the first invalid entry
    while (entry->entrylo & TLBLO_VALID)
    {
        curr_index = (curr_index + 1) % HPT_SIZE;

        if (curr_index == index)
        {
            free_kpages(paddr);
            return ENOMEM; // No invalid entry found, return error
        }

        entry = &hpt[curr_index];
    }

    // internal collision based on the initial hashed index
    struct hpt_entry *curr_entry = &hpt[index];
    if (curr_entry->entrylo & TLBLO_VALID)
    {
        while (curr_entry->next)
            curr_entry = curr_entry->next;
        curr_entry->next = entry;
    }

    // zero pad the page
    zero_pad(paddr, 1);

    entry->pid = (uint32_t)as;
    entry->entryhi = vaddr;
    entry->entrylo = KVADDR_TO_PADDR(paddr) | TLBLO_VALID;
    entry->next = NULL;

    // set TLB_LODIRTY if writeable
    if (permissions & WRITE)
        entry->entrylo |= TLBLO_DIRTY;

    // new tlb entry
    if (write_to_tlb)
    {
        int spl = splhigh();
        tlb_random(entry->entryhi, entry->entrylo);
        splx(spl);
    }

    return 0;
}

/*
    Retrieves the page table entry for the given address space and virtual address.
    Also returns the previous entry in the collision chain if requested.
*/
struct hpt_entry *hpt_get(struct addrspace *as, vaddr_t vaddr, struct hpt_entry **prev_entry)
{
    // Ensure that the virtual address is page-aligned
    KASSERT((vaddr & PAGE_FRAME) == vaddr);

    // Hash the address and get the process ID
    uint32_t index = hpt_hash(as, vaddr);
    pid_t pid = (uint32_t)as;

    // Start at the hashed index and search the linked list of entries for a match
    struct hpt_entry *curr_entry = &hpt[index];
    while (curr_entry != NULL)
    {
        // If the entry matches the virtual address, process ID, and is valid, break out of the loop
        if ((curr_entry->entryhi & TLBHI_VPAGE) == vaddr &&
            pid == curr_entry->pid &&
            (curr_entry->entrylo & TLBLO_VALID))
            break;
        if (prev_entry)
            *prev_entry = curr_entry;
        curr_entry = curr_entry->next;
    }

    // Return the matching entry, or NULL if no match was found
    return curr_entry;
}

/*
    Copies the page table entries and their associated pages
    from the old address space to the new address space for the given memory region.
*/
int hpt_copy(struct as_regions *region, struct addrspace *old, struct addrspace *newas)
{
    vaddr_t addr = region->base;
    vaddr_t end = region->base + region->size;
    while (addr != end)
    {
        // Get the page table entry for the address in the old addrspace.
        struct hpt_entry *entry = hpt_get(old, addr, NULL);
        if (entry != NULL)
        { // found a page entry

            // Add a new page entry to the new addrspace with the same address and permissions as the old entry.
            int ret = hpt_add(newas, addr, region->permissions, false);
            if (ret)
                return ret;

            // Get the new page entry to copy the page contents to.
            struct hpt_entry *new_entry = hpt_get(newas, addr, NULL);
            if (new_entry == NULL)
                return ENOMEM;

            // Copy the contents of the old page to the new page.
            vaddr_t old_frame = PADDR_TO_KVADDR(entry->entrylo & TLBLO_PPAGE);
            vaddr_t new_frame = PADDR_TO_KVADDR(new_entry->entrylo & TLBLO_PPAGE);
            memmove((void *)new_frame, (const void *)old_frame, PAGE_SIZE);
        }
        addr += PAGE_SIZE;
    }
    return 0;
}

void set_page_write_permissions(struct addrspace *as, vaddr_t vaddr, uint32_t memsize)
{
    for (vaddr_t addr = vaddr; addr != vaddr + memsize; addr += PAGE_SIZE)
    {
        addr &= PAGE_FRAME;
        // Get the page table entry for the address
        struct hpt_entry *entry = hpt_get(as, addr, NULL);
        if (entry)
        {
            // If the page table entry exists, update the permissions and write to the TLB
            paddr_t paddr = entry->entrylo & TLBLO_PPAGE;
            entry->entrylo |= TLBLO_DIRTY | TLBLO_VALID;
            int spl = splhigh(); // Disable interrupts
            tlb_write(addr, paddr, entry->entrylo); // Write the updated entry to the TLB
            splx(spl); // Re-enable interrupts
        }
    }
}

void reset_page_write_permissions(struct addrspace *as, vaddr_t vaddr, uint32_t memsize)
{
    for (vaddr_t addr = vaddr; addr != vaddr + memsize; addr += PAGE_SIZE)
    {
        addr &= PAGE_FRAME;
        struct hpt_entry *entry = hpt_get(as, addr, NULL);
        if (entry)
        {
            paddr_t paddr = entry->entrylo & TLBLO_PPAGE;
            entry->entrylo &= ~TLBLO_DIRTY;
            int spl = splhigh();
            tlb_write(addr, paddr, entry->entrylo);
            splx(spl);
        }
    }
}

/*
    Frees the page table entries and associated pages for the specified memory region in the given address space.
*/
void hpt_free(struct addrspace *as, vaddr_t vaddr, uint32_t memsize)
{

    for (vaddr_t page = vaddr; page != vaddr + memsize; page += PAGE_SIZE)
    {
        struct hpt_entry *prev_entry = NULL;
        struct hpt_entry *entry = hpt_get(as, page, &prev_entry);

        // If there is no entry for the current page, skip to the next page
        if (entry == NULL)
            continue;

        KASSERT((entry->entryhi & TLBHI_VPAGE) == page);

        // Free the kernel page allocated for the current page's physical address
        free_kpages(PADDR_TO_KVADDR(entry->entrylo & TLBLO_PPAGE));

        struct hpt_entry *remove_entry = entry;
        if (prev_entry != NULL)
        {
            prev_entry->next = entry->next;
        }
        else if (entry->next != NULL)
        {
            struct hpt_entry *end_entry = entry;
            prev_entry = end_entry;
            // Find the last entry in the chain for the current page's hashed index
            while (end_entry->next != NULL)
            {
                prev_entry = end_entry;
                end_entry = end_entry->next;
            }

            // Replace the current page's entry with the last entry in the chain
            entry->pid = end_entry->pid;
            entry->entryhi = end_entry->entryhi;
            entry->entrylo = end_entry->entrylo;
            remove_entry = end_entry;
            prev_entry->next = NULL; // removing the last entry
        }

        // Clear the removed entry's fields
        remove_entry->pid = 0;
        remove_entry->entryhi = 0;
        remove_entry->entrylo = 0;
    }
}

/*
    Initializes the virtual memory subsystem by calling the hpt_init function.
*/
void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.
     *
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
    hpt_init();
}

/*
    Handles virtual memory faults by checking the fault type and address,
    validating the address space and region, and either adding a new page table entry or updating the TLB with an existing one.
*/
int vm_fault(int faulttype, vaddr_t faultaddress)
{
    struct addrspace *as = proc_getas();
    // check valid region
    if (as->regions == NULL)
        return EFAULT;

    // check permissions
    if (faulttype == VM_FAULT_READONLY)
        return EFAULT;
    if (!(faulttype == VM_FAULT_READ || faulttype == VM_FAULT_WRITE))
        return EINVAL;

    
    struct as_regions *found_region = NULL;

    // Align fault address to a page boundary
    faultaddress &= PAGE_FRAME;

    // Iterate through regions to find the one that contains the fault address
    for (struct as_regions *region = as->regions; region != NULL; region = region->next)
    {
        KASSERT((region->base & PAGE_FRAME) == region->base); // ensure aligned to page boundary
        if (!found_region && faultaddress >= region->base && faultaddress < region->base + region->size)
        {
            found_region = region;
        }
    }
    if (!found_region)
        return EFAULT;

    // Check if the fault type is allowed by the region permissions
    if (faulttype == VM_FAULT_READ && !(found_region->permissions & READ))
    {
        return EFAULT;
    }
    else if ((faulttype == VM_FAULT_WRITE || faulttype == VM_FAULT_READONLY) && !(found_region->permissions & WRITE))
    {
        return EFAULT;
    }



    // Get the corresponding page table entry for the fault address
    struct hpt_entry *entry = hpt_get(as, faultaddress, NULL);

    // If no entry was found, create a new entry in the page table and update the TLB
    if (!entry)
    {
        int ret = hpt_add(as, faultaddress, found_region->permissions, true);
        if (ret)
            return ret;
    }
    else
    {
        // If an entry already exists, update the TLB with the entry
        int spl = splhigh();
        tlb_random(entry->entryhi, entry->entrylo);
        splx(spl);
    }

    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    kfree(hpt);
    panic("vm tried to do tlb shootdown?!\n");
}
