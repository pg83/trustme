//@ compile-flags: -Zdump-mir
#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn echo_nonzero(value: u32) -> u32 {
    let result: u32;
    unsafe {
        core::arch::asm!(
            "test {value:e}, {value:e}",
            "jz {zero}",
            "mov {result:e}, {value:e}",
            value = in(reg) value,
            result = out(reg) result,
            zero = label { std::process::abort() },
            options(nomem, nostack),
        );
    }
    result
}

fn main() {
    assert_eq!(echo_nonzero(19), 19);
    assert_eq!(echo_nonzero(37), 37);
}
