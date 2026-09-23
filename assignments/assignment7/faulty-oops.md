# Topic
This is about how to analyze a kernel oops. As an example a kernel module called faulty is used which triggers a kernel oops on purpose

# printk message to console control
The easiest way to get trace about the problem is to make sure it is printed directly into the console. It can be controlled through /proc/sys/kernel/printk. The first number indicates the current least kernel message level which is printed on the console currently, make sure it is at least 1 to get the trace printed. In my case it was 7 without any adjustment

```
# cat /proc/sys/kernel/printk
7       4       1       7
``` 
# Example output and interpretation
Below is shown what happens when the kernel oops gets trigged by writing anything to /dev/faulty, the device file for the faulty driver
```
# echo “hello_world” > /dev/faulty
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
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b3e000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: faulty(O) hello(O) scull(O)
CPU: 0 PID: 156 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008df3d20
x29: ffffffc008df3d80 x28: ffffff8001b9b500 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008df3dc0
x20: 00000055940c1a00 x19: ffffff8001b8d800 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc00078c000 x3 : ffffffc008df3dc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
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
The line which indicates most clearly is the first one:
`Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000`

To find the code which causes the problem I was able to find these lines in the output
```
pc : faulty_write+0x10/0x20 [faulty]
Call trace:
 faulty_write+0x10/0x20 [faulty]
```
This helps to locate the assembler instruction which fails within the output of objdump which is:
```
000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
   8:   d503233f        paciasp
   c:   d50323bf        autiasp
  10:   b900003f        str     wzr, [x1]
  14:   d65f03c0        ret
  18:   d503201f        nop
  1c:   d503201f        nop
```
The '+0x10' tells the offset of the faulty instruction from the start of the function, that means this line is actually the bad assembler instruction
`10:   b900003f        str     wzr, [x1]`
It tries to store a value (actually 0) to the address which is in x1. At the very first line of the function x1 was filled with 0 which causes an attempt to write to address 0:
```
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
                loff_t *pos)
{
        /* make a simple fault by dereferencing a NULL pointer */
        *(int *)0 = 0;
        return 0;
}
```

# Summary
The trace confirms clearly the kernel oops was caused by `*(int *)0 = 0;` which is in the affected code on purpose to practice the kernel oops root-cause analysis
