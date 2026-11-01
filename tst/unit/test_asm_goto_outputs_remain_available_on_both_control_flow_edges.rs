#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn choose(flag: usize) -> usize {
    let value: usize;
    let mut bonus = 0;
    unsafe {
        core::arch::asm!(
            "mov {value}, 32",
            "test {flag}, {flag}",
            "jz {done}",
            value = out(reg) value,
            flag = in(reg) flag,
            done = label { bonus = 10; },
            options(nomem, nostack),
        );
    }
    value + bonus
}

fn main() {
    assert_eq!(choose(0), 42);
    assert_eq!(choose(1), 32);
}
