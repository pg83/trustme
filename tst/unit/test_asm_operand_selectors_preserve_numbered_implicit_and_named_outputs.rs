fn main() {
    let mut numbered = 17u32;
    unsafe {
        core::arch::asm!("add {0:e}, 5", inout(reg) numbered, options(nomem, nostack));
    }
    assert_eq!(numbered, 22);

    let mut implicit = 23u32;
    unsafe {
        core::arch::asm!("add {:e}, 7", inout(reg) implicit, options(nomem, nostack));
    }
    assert_eq!(implicit, 30);

    let mut named = 31u32;
    unsafe {
        core::arch::asm!("add {value:e}, 11", value = inout(reg) named, options(nomem, nostack));
    }
    assert_eq!(named, 42);
}
