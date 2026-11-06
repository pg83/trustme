use std::arch::asm;

fn main() {
    let input = std::hint::black_box(17usize);
    let first: usize;
    let second: usize;
    unsafe {
        asm!("add {value}, 5", value = inout(reg) input => first, options(nomem, nostack));
        asm!("add {value}, 9", value = inlateout(reg) input => second, options(nomem, nostack));
        asm!("xor eax, eax", out("eax") _, options(nomem, nostack));
        asm!("xor ecx, ecx", lateout("ecx") _, options(nomem, nostack));
        asm!("", clobber_abi("C"), options(nomem, nostack));
    }
    assert_eq!(input, 17);
    assert_eq!(first, 22);
    assert_eq!(second, 26);
}
