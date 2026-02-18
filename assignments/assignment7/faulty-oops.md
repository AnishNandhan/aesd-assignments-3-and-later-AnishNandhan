# Analysis of Kernel Oops caused by "faulty" module

### The call trace

```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b57000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: scull(O) faulty(O) hello(O)
CPU: 0 PID: 156 Comm: sh Tainted: G           O       6.1.44 #2
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008debd20
x29: ffffffc008debd80 x28: ffffff8001ba3500 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000006 x22: 0000000000000006 x21: ffffffc008debdc0
x20: 000000556842aa60 x19: ffffff8001c21900 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000785000 x3 : ffffffc008debdc0
x2 : 0000000000000006 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

We see that the crash happens at 0x10 bytes into the `faulty_write` function.

We can use the `nm` tool to list the symbols and their offsets in the faulty module:
```
./output/host/bin/aarch64-linux-nm ./output/build/ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.ko
                 U __arch_copy_to_user
0000000000000080 T cleanup_module
0000000000000080 T faulty_cleanup
0000000000000000 D faulty_fops
0000000000000020 T faulty_init
0000000000000000 B faulty_major
00000000000000c0 T faulty_read
0000000000000000 T faulty_write
0000000000000020 T init_module
                 U memset
0000000000000000 r _note_10
0000000000000018 r _note_9
                 U __register_chrdev
                 U __stack_chk_fail
0000000000000000 D __this_module
0000000000000000 d __UNIQUE_ID___addressable_cleanup_module223
0000000000000000 d __UNIQUE_ID___addressable_init_module222
0000000000000015 r __UNIQUE_ID_depends223
0000000000000000 r __UNIQUE_ID_license221
000000000000001e r __UNIQUE_ID_name222
000000000000002a r __UNIQUE_ID_vermagic221
                 U __unregister_chrdev
```

The `faulty_write` function is at offset 0000000000000000.

We can use `addr2line` tool to get the exact line number in the source code where the crash occurs:
```
./output/host/bin/aarch64-linux-addr2line -e ./output/build/ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.ko 0x0000000000000000+0x10
/home/anish/AELD/aesd-assignment-4-AnishNandhan/buildroot/output/build/linux-6.1.44/../ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.c:53
```

### Using objdump to dissassemble the module

We can use `objdump` tool to make more sense of the information we have till now:
```
./output/host/bin/aarch64-linux-objdump -d -S ./output/build/ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.ko 

./output/build/ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:

ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
   0:	d2800001 	mov	x1, #0x0                   	// #0
	return 0;
}
   4:	d2800000 	mov	x0, #0x0                   	// #0
{
   8:	d503233f 	paciasp
}
   c:	d50323bf 	autiasp
	*(int *)0 = 0;
  10:	b900003f 	str	wzr, [x1]
}
  14:	d65f03c0 	ret
  18:	d503201f 	nop
  1c:	d503201f 	nop

0000000000000020 <faulty_init>:
	.owner = THIS_MODULE
};

.......
```

At offset 0x10 into the faulty write function, we store into the address pointed to by x1 register the value in wzr (which is always 0).

But we can see that two instructions earlier we store 0 into x1. This means we are trying access address 0x0, which is generally not accessible (in this case the function function `faulty_write` resides there). We cannot modify our own instructions at runtime :)


### GDB analysis
Executing `./output/host/bin/aarch64-linux-gdb ./output/build/linux-6.1.44/vmlinux` and starting qemu with `-s -S`.

In order to set breakpoint at `faulty_write`, we need to add symbols from the module. From the QEMU session we identofy the section addresses:
```
# cat /sys/module/faulty/sections/.text
0xffffffc000785000
# cat /sys/module/faulty/sections/.data
0xffffffc000787000
# cat /sys/module/faulty/sections/.bss
0xffffffc000787400
```

We use these addresses in the GDB session on the host:
```
add-symbol-file ./output/build/ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.ko 0xffffffc000785000 -s .data 0xffffffc000787000 -s .bss 0xffffffc000787400
```

Now we can add breakpoint at `faulty_write`:
```
b faulty_write
```

From the QEMU session, if we perform any write operation on the faulty device: `echo "test" > /dev/faulty`, breakpoint triggers in GDB:
```
(gdb) b faulty_write
Breakpoint 1 at 0xffffffc000785000: file ../ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.c, line 53.
(gdb) continue
Continuing.

Breakpoint 1, faulty_write (filp=0xffffff8001aeda00, buf=0x5564bcadc0 "test\n", count=5, pos=0xffffffc008de3dc0) at ../ldd-72d6b180b4608c8f6584882e59e4d04f2b1e80a4/misc-modules/faulty.c:53
53		*(int *)0 = 0;
(gdb) n
vectors () at arch/arm64/kernel/entry.S:508
508		kernel_ventry	1, h, 64, sync		// Synchronous EL1h
(gdb) n
el1h_64_sync () at arch/arm64/kernel/entry.S:576
576		entry_handler	1, h, 64, sync
(gdb) n
^C      
Program received signal SIGINT, Interrupt.
cpu_do_idle () at arch/arm64/kernel/idle.c:32
32		arm_cpuidle_restore_irq_context(&context);
(gdb)
```

We can see that there is an illegal memory reference in the `faulty_write` function, which is consistent with the previous analysis.
