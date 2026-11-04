//@ compile-variant: -O
#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn unit_path(flag: u64) {
    unsafe {
        std::arch::asm!(
            "test {flag}, {flag}", "jz {waiting}",
            flag = in(reg) flag,
            waiting = label { loop { std::hint::spin_loop(); } },
            options(nostack, nomem),
        );
    }
}

#[inline(never)]
fn value_path(flag: u64) -> u64 {
    let output: u64;
    unsafe {
        std::arch::asm!(
            "mov {output}, 29", "test {flag}, {flag}", "jz {waiting}",
            flag = in(reg) flag,
            output = out(reg) output,
            waiting = label { loop { std::hint::spin_loop(); } },
            options(nostack, nomem),
        );
    }
    output
}

fn main() {
    unit_path(std::hint::black_box(1));
    assert_eq!(value_path(std::hint::black_box(1)), 29);
}
