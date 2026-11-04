//@ compile-variant: -O
#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn selected(flag: u64) -> u64 {
    let output: u64;
    unsafe {
        std::arch::asm!(
            "mov {output}, 23",
            "test {flag}, {flag}",
            "jz {jump}",
            flag = in(reg) flag,
            output = out(reg) output,
            jump = label { return 17; },
            options(nostack, nomem),
        );
    }
    output
}

fn main() {
    assert_eq!(selected(std::hint::black_box(0)), 17);
    assert_eq!(selected(std::hint::black_box(1)), 23);
}
