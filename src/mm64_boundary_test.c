#include <assert.h>
#include <stdio.h>

#include "../include/mm64.h"

int main(void)
{
  addr_t last_dynamic_page = KERNEL_DYNAMIC_END - (PAGING64_PAGESZ - 1);

  /* Basic sanity on the requested split boundary. */
  assert(KERNEL_DYNAMIC_END + 1ULL == KERNEL_STATIC_START);

  /* Lowest and highest valid dynamic mappings. */
  assert(mm64_kernel_dynamic_range_fits(KERNEL_DYNAMIC_START, 1));
  assert(mm64_kernel_dynamic_range_fits(last_dynamic_page, 1));

  /* Crossing by one page from the last valid page must fail. */
  assert(!mm64_kernel_dynamic_range_fits(last_dynamic_page, 2));

  /* Any mapping that starts in static-kernel region must fail. */
  assert(!mm64_kernel_dynamic_range_fits(KERNEL_STATIC_START, 1));

  /* Invalid request sizes must fail. */
  assert(!mm64_kernel_dynamic_range_fits(KERNEL_DYNAMIC_START, 0));

  printf("mm64 dynamic-kernel boundary test: PASS\n");
  return 0;
}
