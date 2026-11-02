//@ compile-variant: -O
#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn ordinary(mut value: u64) -> u64 {
    unsafe { core::arch::asm!("add {value}, {amount}", value = inout(reg) value, amount = const 17, options(nomem, nostack)); }
    value
}

#[inline(never)]
fn branching(mut value: u64) -> (u64, u8) {
    let mut reached = 0;
    unsafe {
        core::arch::asm!("add {value}, {amount}", "jmp {done}",
            value = inout(reg) value, amount = const 17,
            done = label { reached = 3; }, options(nomem, nostack));
    }
    (value, reached)
}

fn main() {
    assert_eq!(ordinary(std::hint::black_box(5)), 22);
    assert_eq!(branching(std::hint::black_box(5)), (22, 3));
}
