//@ compile-flags: -Z mir-opt-level=2 -Z dump-mir
#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn copied_before_output(input: u64, branch: u64) -> u64 {
    let mut source = (input,);
    let saved = (source,);
    unsafe {
        std::arch::asm!(
            "mov {output}, 9",
            "test {branch}, {branch}",
            "jz {zero}",
            output = out(reg) source.0,
            branch = in(reg) branch,
            zero = label { return 0; },
            options(nomem, nostack),
        );
    }
    let result = saved.0.0 + source.0;
    std::hint::black_box(&saved);
    result
}

fn main() {
    assert_eq!(copied_before_output(11, 1), 20);
    assert_eq!(copied_before_output(17, 1), 26);
    assert_eq!(copied_before_output(11, 0), 0);
}
