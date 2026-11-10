//@ compile-flags: -O -Zdump-mir
#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn choose(flag: u32) -> u32 {
    let value: u32;
    unsafe {
        core::arch::asm!(
            "mov {value:e}, 101",
            "test {flag:e}, {flag:e}",
            "jnz {jump}",
            value = out(reg) value,
            flag = in(reg) flag,
            jump = label { return 107; },
            options(nomem, nostack),
        );
    }
    value
}

#[inline(never)]
fn store(flag: u32, output: &mut u32) {
    unsafe {
        core::arch::asm!(
            "mov dword ptr [{output}], 113",
            "test {flag:e}, {flag:e}",
            "jnz {jump}",
            output = in(reg) (output as *mut u32),
            flag = in(reg) flag,
            jump = label { *output = 127; return; },
            options(nostack),
        );
    }
}

fn main() {
    assert_eq!(choose(std::hint::black_box(0)), 101);
    assert_eq!(choose(std::hint::black_box(1)), 107);
    let mut output = 0;
    store(std::hint::black_box(0), &mut output);
    assert_eq!(output, 113);
    store(std::hint::black_box(1), &mut output);
    assert_eq!(output, 127);
}
