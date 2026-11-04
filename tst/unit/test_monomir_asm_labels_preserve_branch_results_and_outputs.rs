//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0
#![feature(asm_goto_with_outputs)]
pub fn choose(value: u32) -> u32 {
    let output: u32;
    unsafe {
        core::arch::asm!(
            "test {value:e}, {value:e}", "jz {zero}", "mov {output:e}, 71",
            value = in(reg) value, output = lateout(reg) output,
            zero = label { return 73; }, options(nostack)
        );
    }
    output
}
pub fn add(value: u32) -> u32 {
    let output: u32;
    unsafe { core::arch::asm!("lea {output:e}, [{value:e} + 79]", value = in(reg) value,
        output = lateout(reg) output, options(nomem, nostack)); }
    output
}
