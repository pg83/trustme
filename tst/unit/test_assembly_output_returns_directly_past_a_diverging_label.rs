//@ compile-flags: -O
#![feature(asm_goto_with_outputs)]

fn answer() -> u32 {
    let value;
    unsafe {
        core::arch::asm!(
            "mov {value:e}, 179",
            "/* {blocked} */",
            value = lateout(reg) value,
            blocked = label { loop {} },
            options(nomem, nostack),
        );
    }
    value
}

fn main() {
    assert_eq!(answer(), 179);
}
