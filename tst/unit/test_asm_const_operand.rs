//@ run-pass
// An immediate assembly operand has to be a value, and a named constant
// reaches MIR as the path that names it; folding it to the value it names is
// what lets `const` operands be written that way.

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        const SHIFT: u32 = 3;
        let mut value: u64 = 1;
        core::arch::asm!("shl {v}, {s}", v = inout(reg) value, s = const SHIFT);
        assert_eq!(value, 8);

        let out: u64;
        core::arch::asm!("mov {o}, {s}", o = out(reg) out, s = const SHIFT);
        assert_eq!(out, 3);
    }
}
