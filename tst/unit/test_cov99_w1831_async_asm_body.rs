//@ crate-type: lib
//@ compile-only
//@ edition: 2021
//@ compile-flags: --emit=metadata -Cpanic=abort -Copt-level=2

pub fn build(flag: bool) {
    let make = async move || {
        if flag {
            unsafe {
                core::arch::asm!(
                    "nop",
                    in("eax") 1_u32,
                    options(nomem, nostack, preserves_flags)
                );
            }
        } else {
            unsafe {
                core::arch::asm!(
                    "nop",
                    in("eax") 1_u32,
                    options(nomem, nostack, preserves_flags)
                );
            }
        }
    };
    let _future = make();
}
