#![feature(asm_goto_with_outputs)]

#[inline(never)]
fn count(mut retry: u32, value: &mut u64) {
    'attempt: loop {
        unsafe {
            core::arch::asm!(
                "inc qword ptr [{destination}]",
                "test {retry:e}, {retry:e}",
                "jnz {again}",
                destination = in(reg) (value as *mut u64),
                retry = in(reg) retry,
                again = label { retry = 0; continue 'attempt; },
                options(nostack),
            );
        }
        return;
    }
}

fn main() {
    let mut value = 4;
    count(std::hint::black_box(0), &mut value);
    assert_eq!(value, 5);
    count(std::hint::black_box(1), &mut value);
    assert_eq!(value, 7);
}
