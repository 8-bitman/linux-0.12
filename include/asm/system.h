// 定义了设置或修改描述符/中断门等的嵌入式汇编宏.

/**
 * 嵌入汇编格式: (参见 CLK p50)
 *
 * asm("汇编语句"
 * 	   : 输出寄存器 								// 编号 %0, %1, %2...
 *     : 输入寄存器 								// 编号 %3, %4, %5...
 * 	   : 会被修改的寄存器);
 *
 * 输出寄存器: 表示这段汇编执行完之后, 哪些寄存器用于存放输出数据.
 * 输入寄存器: 表示在开始执行汇编代码时, 指定一些寄存器存放输入值.
 * 会被修改的寄存器: 表示又对这些寄存器的值进行了改动, gcc 不能再以为里面保存的是原值.
 */

/**
 * 在发生特权级变化的任务切换时, 会发生堆栈切换操作. 过程如下[原本为处理器自动操作, 现在是进行手动模拟]
 * (即下面要模拟的任务 0 切换到内核模式下运行的过程[push 操作]):
 * 1. 从当前任务的 tss 段中得到中断或异常处理要使用的堆栈选择符和栈指针(tss.ss0, tss.esp0). 
 *                 	[特权级不发生变化时, 不会切换堆栈, 中断处理程序会继续使用当前任务的堆栈]
 * 2. 处理器把当前被中断任务的栈选择符(ss)和栈指针(esp)在压入 **新栈** 中(中断或异常等处理过程使用的栈). 
 *                	[特权级不发生变化时不会保存 ss:esp]
 * 3. 把当前任务的 EFLAGS, cs, eip 寄存器的值也压入 **新栈** 中.
 */
/**
 * 执行 iret 指令通过中断返回切换回原任务发生特权级变化时, 会执行以下操作: 
 * 1. 恢复原任务(此处是 TASK0)的 EIP 寄存器.
 * 2. 恢复原任务的 CS 寄存器.
 * 3. 恢复原任务的 EFLAGS 寄存器.
 * 4. 恢复原任务的 ESP 寄存器. (特权级发生变化时会恢复原任务的 ss:esp 造成堆栈的切换, 特权级不发生变化时不会切换堆栈)
 * 5. 恢复原任务的 SS 寄存器. [堆栈切换完成]
 */

// 模拟中断返回实现转移到用户态下运行.
// 该函数利用 iret 指令实现从内核态移动到用户态的初始任务 0 中去执行. 
// [实际上就是先模拟从任务 0 调用中断时的中断处理过程(详见 chapter 4.6.9), 
// 再通过调用 iret 实现中断返回, 返回到任务 0 上执行.]
// 中断调用时会切换到新栈(堆栈切换), 并依次入栈: ss, esp, eflags, cs, eip.
// 中断返回时会依次从栈中加载原: eip(栈顶), cs, eflags, esp, ss(栈底) 这几个寄存器的值.
// 先构造任务 0 调用中断时压栈的场景，再利用 iret 来切换到任务 0.
// 以后的汇编代码都是 AT&T 格式的: movl %esp, %eax ==> 将 esp 中的值放到 eax 中.
// %eax 表示寄存器中的值, (%eax) 表示 eax 指向的内存中的值.
// 要压栈的数据参见: p122-123 -> 图 4-28 和 图 4-29.
// 注意, 通过以下代码分析, TASK0 是使用 LDT 中的的数据段 0x17 = 0b-00010-111[位 2(TI) = 1 表示 LDT].
// !!! 在前面的 sched_init() 函数中已经将 TASK0 的 ldt 和 tss 分别加载到 ldtr 和 tr 寄存器中了.
// 注意, 这里中断返回和任务切换时的操作不一样, 并不会从 TSS 加载任务状态信息, 中断会自动压栈一些寄存器信息, 
// 中断返回时使用这些信息恢复任务执行. 即中断和任务切换时的入栈信息及恢复信息不是一回事.
#define move_to_user_mode() \
__asm__ (\
	"movl %%esp, %%eax;"  				/* 保存堆栈指针 esp 到 eax 寄存器中. */\
	"pushl $0x17;"  					/* 首先将任务 0 的堆栈段选择符(SS)入栈: 0x17 -> LDT 中项 2(数据段) */\
	"pushl %%eax;"  					/* 然后将保存的任务 0 的堆栈指针值(esp)入栈 */\
	"pushfl;"  							/* 将标志寄存器(eflags)内容入栈 */\
	"pushl $0x0f;"  					/* 将任务 0 代码段选择符(cs)入栈. 0x0f -> LDT 中项 1(代码段) CPL = 3 */\
	"pushl $1f;"  						/* 将下面标号 1 的偏移地址(eip)入栈. */\
	"iret;"  							/* 执行中断返回指令(此处只是实现了特权级切换, 而不是真正的任务切换), 则会跳转到下面标号 1 处. */\
	"1: movl $0x17, %%eax;"  			/* 此时开始以用户态继续执行任务 0 */ /* 任务 0 使用 LDT 中的数据空间了 */\
	"mov %%ax, %%ds;"  					\
	"mov %%ax, %%es;" 					\
	"mov %%ax, %%fs;" 					\
	"mov %%ax, %%gs" 					/* 初始化段寄存器指向本局部表数据段, 此时所有段寄存器都指向 TASK0 的数据段 */\
	: : : "ax") 						// 输出寄存器, 输入寄存器为空, 修改过寄存器 eax 中的值.

#define sti() __asm__ ("sti"::)				// 开中断嵌入汇编宏函数.
#define cli() __asm__ ("cli"::)				// 关中断.
#define nop() __asm__ ("nop"::)				// 空操作.

#define iret() __asm__ ("iret"::)			// 中断返回

// 设置门描述符宏.
// 门描述符中的 2-3 字节是存放处理程序的段选择符.
// 根据参数中的中断或异常过程地址 addr, 门描述符类型 type 和特权级信息 dpl, 设置位于地址 gate_addr 处的门描述符. 
// (注意: 下面 "偏移" 值是相对于内核代码或数据段来说的).
// 参数: gate_addr - 描述符所在地址; type - 描述符类型域值; dpl - 描述符特权级; addr - 中断/异常处理程序偏移地址.
// %0 - (由 dpl, type 组合成的类型标志字); %1 - (描述符低 4 字节地址);
// %2 - (描述符高4字节地址); %3 - edx(程序偏移地址 addr); %4 - eax(高字中含有内核代码段选择符 0x8).
#define _set_gate(gate_addr, type, dpl, addr) \
__asm__ (\
	"movw %%dx, %%ax;"  				/* 将处理程序偏移地址低字选择符组合成描述符低 4 字节(eax) */\
	"movw %0, %%dx;"  					/* 将类型标志字与偏移高字组合成描述符高 4 字节(edx) */\
	"movl %%eax, %1;"  					/* 分别设置门描述符的低 4 字节和高 4 字节 */\
	"movl %%edx, %2" \
	: \
	: "i" ((short) (0x8000 + (dpl << 13) + (type << 8))), \
	  "o" (*((char *) (gate_addr))), \
	  "o" (*(4 + (char *) (gate_addr))), \
	  "d" ((char *) (addr)), \
	  "a" (0x00080000))					/* d -> edx, a -> eax */

// 设置中断门函数(自动屏蔽随后的中断). 
// 陷阱门和中断门是调用门的特殊类, 调用门用于在不同特权级代码段之间实现受控的程序转移; 
// 中断门和陷阱门描述符有一个长指针(即段选择符 + 偏移值)，
// 处理器使用这个长指针把程序执行权转移到代码段中异常或中断的处理过程中.
// 参数: n - 中断号; addr - 中断程序偏移地址.
// &idt[n] 是中断描述表中中断号 n 对应项的偏移值; 中断描述符的类型是 14(中断门), 特权级是 0.
#define set_intr_gate(n, addr) \
	_set_gate(&idt[n], 14, 0, addr)

// 设置陷阱门函数(DPL = 0). 陷阱门用于处理异常(故障, 陷阱, 中止).
// 参数: n - 中断号; addr - 中断程序偏移地址.
// &idt[n] 是中断描述符表中中断号 n 对应项的偏移值; 中断描述符的类型是 15, 特权级是 0.
#define set_trap_gate(n, addr) \
	_set_gate(&idt[n], 15, 0, addr)

// 设置系统**陷阱门**函数(特权级 DPL = 3).
// 上面 set_trap_gate() 设置的描述符的特权级(DPL)为 0, 而这里是 3. 
// 因此 set_system_gate() 设置的中断处理过程能够被所有特权级的程序执行, 
// 比如 system_call 陷阱门的 DPL = 3, 这表示用户态的代码可以调用这个陷阱门, 从而实现对系统代码的调用.
// 例如单步调试, 溢出出错和边界超出出错处理.
// 参数: n - 中断号. addr - 中断程序偏移值.
// &idt[n] 是中断描述符表中中断号 n 对应项的偏移值; 
// 中断描述符的类型是 15(陷阱门), 特权级 DPL 是 3(所有特权级的代码都可以调用这类门描述符).
#define set_system_gate(n, addr) \
	_set_gate(&idt[n], 15, 3, addr)

// 设置段描述符函数(内核中没有用到).
// 参数: gate_addr - 描述符地址; type - 描述符中类型域值; 
// dpl - 描述符特权层值; base - 段的基地址; limit - 段限长.
#define _set_seg_desc(gate_addr, type, dpl, base, limit) {\
	*((gate_addr) + 1) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000) >> 16) | \
		((limit) & 0xf0000) | \
		((dpl) << 13) | \
		(0x00408000) | \
		((type) << 8); \
	*(gate_addr) = (((base) & 0x0000ffff) << 16) | \
		((limit) & 0x0ffff); }

// 在全局表中设置任务状态段/局部表描述符. 状态段局部描述符表段的长度均被设置成 104 字节.
// 参数: n - 在全局表中描述符项 n 所对应的地址; addr - 状态段/局部表所在内存的基地址. 
// 		type - 描述符中的类型标志(TYPE: 位 40-43)字节.
// %0 - eax(地址 addr); %1 - (描述符项 n 的地址); %2 - (描述符项 n 的地址偏移 2 处);
// %3 - (描述符项 n 的地址偏移 4 处); %4 - (描述符项 n 的地址偏移 5 处); 
// %5 - (描述符项 n 的地址偏移 6 处); %6 - (描述符项 n 的地址偏移 7 处);
#define _set_tssldt_desc(n, addr, type) \
__asm__ (\
	"movw $104, %1\n\t"							/* 将 TSS(或 LDT)长度放入描述符长度域(第 0-1 字节) */\
	"movw %%ax, %2\n\t"							/* 将基地址的低字放入描述符第 2-3 字节 */\
	"rorl $16, %%eax\n\t"						/* 将基地址高字右循环移入 ax 中(低字则进入高字处) */\
	"movb %%al, %3\n\t"  						/* 将基地址高中低字节移入描述符第 4 字节 */\
	"movb $" type ", %4\n\t"  					/* 将类型标志字节(DPL/TYPE 等)移入描述符第 5(从 0 开始) 字节 */\
	"movb $0x00, %5\n\t"  						/* 描述符第 6 字节置 0 */\
	"movb %%ah, %6\n\t"  						/* 将基地址高字中高字节移入描述符第 7 字节 */\
	"rorl $16, %%eax"  							/* 再右循环 16 比特, eax 恢复原值. */\
	::"a" (addr), "m" (*(n)), "m" (*(n + 2)), "m" (*(n + 4)), \
	 "m" (*(n + 5)), "m" (*(n + 6)), "m" (*(n + 7)) \
	)

// 在全局表中设置任务状态段描述符.
// n - 是该描述符的指针; addr - 是描述符项中段的基地址值. // 任务状态段的 DPL 是 0(只有特权级 CPL 为 0 的代码才可以调用该任务以执行任务切换).
#define set_tss_desc(n, addr) _set_tssldt_desc(((char *) (n)), addr, "0x89") 	// 0x89 - 0b-1000(P = 0b1; DPL = 0b00; 0b0) - 1001(TYPE = TSS).
// 在全局表中设置局部表描述符.
// n - 是该描述符的指针; addr - 是描述符项中段的基地址值. 局部表段描述符的类型是 0x82. 	   // DPL 是 0.
#define set_ldt_desc(n, addr) _set_tssldt_desc(((char *) (n)), addr, "0x82") 	// 0x82 - 0b-1000-0010
