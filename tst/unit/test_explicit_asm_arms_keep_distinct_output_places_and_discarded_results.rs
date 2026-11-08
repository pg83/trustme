//@ compile-flags: -O

#[inline(never)]
fn assign(choice: u8) -> (u64, u64) {
    let mut left = 11;
    let mut right = 29;
    match choice {
        0 => unsafe {
            core::arch::asm!("mov rax, 7", lateout("rax") left, options(nomem, nostack));
        },
        1 => unsafe {
            core::arch::asm!("mov rax, 7", lateout("rax") right, options(nomem, nostack));
        },
        _ => unsafe {
            core::arch::asm!("mov rax, 7", lateout("rax") _, options(nomem, nostack));
        },
    }
    (left, right)
}

fn main() {
    assert_eq!(assign(0), (7, 29));
    assert_eq!(assign(1), (11, 7));
    assert_eq!(assign(2), (11, 29));
}
