#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../include/mm64.h"

static const char *region_name(addr_t addr)
{
  if (IS_USER_SPACE(addr))
    return "USER";
  if (IS_CANONICAL_HOLE(addr))
    return "CANONICAL_HOLE";
  if (IS_KERNEL_DYNAMIC_SPACE(addr))
    return "KERNEL_DYNAMIC";
  if (IS_KERNEL_STATIC_SPACE(addr))
    return "KERNEL_STATIC";
  return "UNKNOWN";
}

int main(void)
{
  addr_t last_dynamic_page = KERNEL_DYNAMIC_END - (PAGING64_PAGESZ - 1);
  addr_t dynamic_tail = KERNEL_DYNAMIC_END;
  addr_t static_head = KERNEL_STATIC_START;

  printf("ReikOS MM64 memory layout\n");
  printf("  USER_SPACE          : 0x%016lx - 0x%016lx\n",
         (unsigned long)USER_SPACE_START,
         (unsigned long)USER_SPACE_END);
  printf("  CANONICAL_HOLE      : 0x%016lx - 0x%016lx\n",
         (unsigned long)CANONICAL_HOLE_START,
         (unsigned long)CANONICAL_HOLE_END);
  printf("  KERNEL_DYNAMIC      : 0x%016lx - 0x%016lx\n",
         (unsigned long)KERNEL_DYNAMIC_START,
         (unsigned long)KERNEL_DYNAMIC_END);
  printf("  KERNEL_STATIC       : 0x%016lx - 0x%016lx\n",
         (unsigned long)KERNEL_STATIC_START,
         (unsigned long)KERNEL_STATIC_END);
  printf("\nRegion samples\n");
  printf("  0x%016lx -> %s\n", (unsigned long)USER_SPACE_START, region_name(USER_SPACE_START));
  printf("  0x%016lx -> %s\n", (unsigned long)CANONICAL_HOLE_START, region_name(CANONICAL_HOLE_START));
  printf("  0x%016lx -> %s\n", (unsigned long)KERNEL_DYNAMIC_START, region_name(KERNEL_DYNAMIC_START));
  printf("  0x%016lx -> %s\n", (unsigned long)KERNEL_STATIC_START, region_name(KERNEL_STATIC_START));

  /* Basic sanity on the requested split boundary. */
  assert(KERNEL_DYNAMIC_END + 1ULL == KERNEL_STATIC_START);
  assert(strcmp(region_name(USER_SPACE_START), "USER") == 0);
  assert(strcmp(region_name(CANONICAL_HOLE_START), "CANONICAL_HOLE") == 0);
  assert(strcmp(region_name(KERNEL_DYNAMIC_START), "KERNEL_DYNAMIC") == 0);
  assert(strcmp(region_name(KERNEL_STATIC_START), "KERNEL_STATIC") == 0);

  /* Lowest and highest valid dynamic mappings. */
  assert(mm64_kernel_dynamic_range_fits(KERNEL_DYNAMIC_START, 1));
  assert(mm64_kernel_dynamic_range_fits(last_dynamic_page, 1));
  assert(mm64_kernel_dynamic_range_fits(dynamic_tail - PAGING64_PAGESZ + 1, 1));

  /* Crossing by one page from the last valid page must fail. */
  assert(!mm64_kernel_dynamic_range_fits(last_dynamic_page, 2));

  /* Any mapping that starts in static-kernel region must fail. */
  assert(!mm64_kernel_dynamic_range_fits(KERNEL_STATIC_START, 1));
  assert(!mm64_kernel_dynamic_range_fits(static_head, 1));

  /* Invalid request sizes must fail. */
  assert(!mm64_kernel_dynamic_range_fits(KERNEL_DYNAMIC_START, 0));

  printf("\nmm64 dynamic-kernel boundary test: PASS\n");
  return 0;
}
