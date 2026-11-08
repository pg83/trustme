//@ compile-flags: -O

#[inline(never)]
fn adjust(choice: u8, mut value: u64) -> u64 {
    match choice {
        0 => unsafe {
            core::arch::asm!("add rax, 9", inout("rax") value, options(nomem, nostack));
        },
        1 => unsafe {
            core::arch::asm!("add rax, 9", inout("rax") value, options(nomem, nostack));
        },
        _ => value += 2,
    }
    value
}

fn main() {
    assert_eq!(adjust(0, 33), 42);
    assert_eq!(adjust(1, 46), 55);
    assert_eq!(adjust(2, 19), 21);
}
