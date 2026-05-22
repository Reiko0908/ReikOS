#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mm64.h"

static const char *region_name(addr_t addr)
{
  if (IS_USER_DYNAMIC_SPACE(addr))
    return "USER_DYNAMIC";
  if (IS_USER_STATIC_SPACE(addr))
    return "USER_STATIC";
  if (IS_CANONICAL_HOLE(addr))
    return "CANONICAL_HOLE";
  if (IS_KRNL_DYNAMIC_SPACE(addr))
    return "KRNL_DYNAMIC";
  if (IS_KRNL_STATIC_SPACE(addr))
    return "KRNL_STATIC";
  return "UNKNOWN";
}

int main(void)
{
  addr_t last_dynamic_page = KRNL_DYNAMIC_END - (PAGING64_PAGESZ - 1);
  addr_t dynamic_tail = KRNL_DYNAMIC_END;
  addr_t static_head = KRNL_STATIC_START;

  printf("ReikOS MM64 memory layout\n");
  printf("  USER_SPACE          : 0x%016lx - 0x%016lx\n",
         (unsigned long)USER_SPACE_START,
         (unsigned long)USER_SPACE_END);
  printf("  CANONICAL_HOLE      : 0x%016lx - 0x%016lx\n",
         (unsigned long)CANONICAL_HOLE_START,
         (unsigned long)CANONICAL_HOLE_END);
  printf("  KRNL_DYNAMIC      : 0x%016lx - 0x%016lx\n",
         (unsigned long)KRNL_DYNAMIC_START,
         (unsigned long)KRNL_DYNAMIC_END);
  printf("  KRNL_STATIC       : 0x%016lx - 0x%016lx\n",
         (unsigned long)KRNL_STATIC_START,
         (unsigned long)KRNL_STATIC_END);
  printf("\nRegion samples\n");
  printf("  0x%016lx -> %s\n", (unsigned long)USER_SPACE_START, region_name(USER_SPACE_START));
  printf("  0x%016lx -> %s\n", (unsigned long)CANONICAL_HOLE_START, region_name(CANONICAL_HOLE_START));
  printf("  0x%016lx -> %s\n", (unsigned long)KRNL_DYNAMIC_START, region_name(KRNL_DYNAMIC_START));
  printf("  0x%016lx -> %s\n", (unsigned long)KRNL_STATIC_START, region_name(KRNL_STATIC_START));

  /* Basic sanity on the requested split boundary. */
  assert(KRNL_DYNAMIC_END + 1ULL == KRNL_STATIC_START);
  assert(strcmp(region_name(USER_SPACE_START), "USER") == 0);
  assert(strcmp(region_name(CANONICAL_HOLE_START), "CANONICAL_HOLE") == 0);
  assert(strcmp(region_name(KRNL_DYNAMIC_START), "KRNL_DYNAMIC") == 0);
  assert(strcmp(region_name(KRNL_STATIC_START), "KRNL_STATIC") == 0);

  /* Lowest and highest valid dynamic mappings. */
  assert(krnl_dynamic_range(KRNL_DYNAMIC_START, 1));
  assert(krnl_dynamic_range(last_dynamic_page, 1));
  assert(krnl_dynamic_range(dynamic_tail - PAGING64_PAGESZ + 1, 1));

  /* Crossing by one page from the last valid page must fail. */
  assert(!krnl_dynamic_range(last_dynamic_page, 2));

  /* Any mapping that starts in static-kernel region must fail. */
  assert(!krnl_dynamic_range(KRNL_STATIC_START, 1));
  assert(!krnl_dynamic_range(static_head, 1));

  /* Invalid request sizes must fail. */
  assert(!krnl_dynamic_range(KRNL_DYNAMIC_START, 0));

  printf("\nmm64 dynamic-kernel boundary test: PASS\n");
  return 0;
}
