## 练习2：完善中断处理 （需要编程）

完善代码：

```c++
clock_set_next_event();
ticks++;
if(ticks == 100)
{
    print_ticks();
    ticks = 0;
    num++;
}
if(num == 10)
{
    sbi_shutdown();
}
```

- **实现过程：**首先调用`clock_set_next_event`函数，设置下次的中断时间，为当前时间加上100000。接着`ticks`自增1，如果`ticks`为100，调用`print_ticks`打印`ticks`，将`ticks`置0，`num`自增1。如果`num`增加到10，调用`sbi_shutdown`关机。

- **定时器中断处理流程：**

  **中断触发**：当定时器中断发生时，CPU 将中断信号传递给处理程序，进入 `interrupt_handler` 函数。

  **识别中断类型**：处理函数首先检查中断原因，通过 `tf->cause` 获取当前的中断类型。定时器中断的类型为 `IRQ_S_TIMER`。

  **设置下次定时器中断**：在 `IRQ_S_TIMER` 分支中，首先调用 `clock_set_next_event()` 函数，设置下一个定时器中断的时间。这是为了确保系统能够继续接收定时器中断。

  **计数器增量**：每当定时器中断被处理时，`ticks` 计数器增加 1。该计数器用于跟踪自上次打印以来发生的中断次数。

  **检查计数器**：代码检查 `ticks` 是否达到 100。如果达到了 100，则调用 `print_ticks()` 函数，打印出 100 个时钟中断的消息，并重置 `ticks` 为 0。同时，将 `num` 计数器加 1，以记录已打印的次数。

  **检查打印次数并关机**：代码随后检查 `num` 是否达到了 10。如果 `num` 达到 10，表示已经处理了 1000 次定时器中断（10 次打印），然后调用 `sbi_shutdown()` 函数进行关机处理。

  **结束处理**：完成所有处理后，`interrupt_handler` 函数结束。控制权将返回到上层代码，继续执行被中断的程序。

  

## 扩增练习Challenge 2：理解上下文切换机制

问题：在`trapentry. S`中汇编代码 `csrw sscratch, sp；``csrrw s0, sscratch, x0`实现了什么操作，目的是什么？`save all`里面保存了`stval scause`这些`csr`，而在`restore all`里面却不还原它们？那这样`store`的意义何在呢？

**`csrw sscratch, sp`**:

- 这条指令的作用是将当前栈指针 `sp` 的值写入 `sscratch` 。`sscratch` 是一个用于保存特定临时值的寄存器，通常用于异常或中断处理时存储必要的信息。

**`csrrw s0, sscratch, x0`**:

- 这条指令的作用是将 `sscratch` 中的值读出并存入寄存器 `s0`，同时将 `x0`（即零寄存器）写入 `sscratch`。结果是将 `sscratch` 清空，并将之前存储在其中的值（即 `sp` 的值）加载到 `s0` 中。

这些操作的主要目的是在处理中断或异常时，保存和恢复当前的执行上下文。通过将栈指针保存到 `sscratch` 中，可以在处理完中断或异常后恢复到正确的执行状态。

不还原那些 `csr`，是因为异常已经由`trap`处理过了，没有必要再去还原。它们包含有关导致异常或中断的信息，这些信息在处理异常或中断时可能仍然需要。在异常或中断处理程序中，这些`csr`可能需要被读取以确定异常的原因或其他相关信息。保存这些寄存器的值允许在处理异常后能够进行调试，查看导致异常的原因。某些情况下，异常处理可能会导致状态的改变。即使不恢复之前的状态，保存的值可以用于后续决策或处理。在处理中断或异常时，保存所有相关寄存器状态有助于保持系统的一致性，以便于进行正确的异常管理。
