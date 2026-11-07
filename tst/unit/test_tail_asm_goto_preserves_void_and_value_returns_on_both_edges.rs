#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn store(flag: u32, value: &mut u64) {
    unsafe {
        core::arch::asm!(
            "mov qword ptr [{destination}], 61",
            "test {flag:e}, {flag:e}",
            "jnz {exit}",
            destination = in(reg) (value as *mut u64),
            flag = in(reg) flag,
            exit = label { *value = 83; return; },
            options(nostack),
        );
    }
}

#[inline(never)]
fn select(flag: u32) -> u64 {
    let value: u64;
    unsafe {
        core::arch::asm!(
            "mov {value}, 67",
            "test {flag:e}, {flag:e}",
            "jnz {exit}",
            value = out(reg) value,
            flag = in(reg) flag,
            exit = label { return 89; },
            options(nostack),
        );
    }
    value
}

fn main() {
    let mut value = 0;
    store(std::hint::black_box(0), &mut value);
    assert_eq!(value, 61);
    store(std::hint::black_box(1), &mut value);
    assert_eq!(value, 83);
    assert_eq!(select(std::hint::black_box(0)), 67);
    assert_eq!(select(std::hint::black_box(1)), 89);
}
