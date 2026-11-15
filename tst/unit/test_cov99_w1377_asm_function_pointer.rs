//@ edition: 2021

fn marker() -> u32 {
    1377
}

fn main() {
    let pointer: fn() -> u32 = marker;
    unsafe {
        core::arch::asm!(
            "/* {pointer} */",
            pointer = in(reg) pointer,
            options(nomem, nostack, preserves_flags)
        );
    }
    assert_eq!(pointer(), 1377);
}
