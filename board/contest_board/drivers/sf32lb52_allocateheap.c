/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_allocateheap.c
 *
 * 堆划分 (对齐 openvela nrf53 约定)
 *
 * 约定: 芯片层必须导出 g_idle_topstack, 堆自其开始;
 *       空余 SRAM 到 mailbox 缓冲区之前全部并入堆。
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "sf32lb52_memorymap.h"

#include <arch/board/board.h>

/* 注意: 不要在这里自行声明 _ebss!
 * arch/arm/src/common/arm_internal.h:200-204 已声明:
 *   EXTERN uint8_t _eronly[];
 *   EXTERN uint8_t _sdata[];
 *   EXTERN uint8_t _sbss[];
 *   EXTERN uint8_t _ebss[];
 * 自行写成 `extern uint32_t _ebss;` 会与之冲突:
 *   error: conflicting types for '_ebss'; have 'uint32_t'
 * nrf53_allocateheap.c 的做法就是不声明、直接使用。
 */

const uintptr_t g_idle_topstack = (uintptr_t)_ebss +
                                  CONFIG_IDLETHREAD_STACKSIZE;

/****************************************************************************
 * Name: up_allocate_heap
 ****************************************************************************/

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  uintptr_t start;
  uintptr_t end;

#ifdef CONFIG_ARCH_LEDS
  board_autoled_on(LED_HEAPALLOCATE);
#endif

  start = (g_idle_topstack + 7) & ~7ul;
  end   = ((uintptr_t)SF32LB52_MBOX_BUF_ADDR) & ~7ul;

  DEBUGASSERT(end > start);

  *heap_start = (void *)start;
  *heap_size  = (size_t)(end - start);
}

#if defined(CONFIG_BUILD_KERNEL) && defined(CONFIG_MM_KERNEL_HEAP)
void up_allocate_kheap(void **heap_start, size_t *heap_size)
{
  board_autoled_on(LED_HEAPALLOCATE);

  *heap_start = (void *)((g_idle_topstack + 7) & ~7ul);
  *heap_size  = (size_t)((((uintptr_t)SF32LB52_MBOX_BUF_ADDR) & ~7ul) -
                          ((g_idle_topstack + 7) & ~7ul));
}
#endif