//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0

pub extern "C" fn destination() {}

pub unsafe fn operands(input: usize) -> usize {
    let output: usize;
    core::arch::asm!("/* {entry} */", entry = sym destination, options(nomem, nostack));
    core::arch::asm!("/* {value} */", value = in(reg) input, options(nomem, nostack));
    core::arch::asm!("mov {value}, 29", value = out(reg) output, options(nomem, nostack));
    core::arch::asm!("", out("rax") _, options(nomem, nostack));
    output
}
