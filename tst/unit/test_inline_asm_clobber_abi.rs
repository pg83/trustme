#![allow(unused_assignments)]

#[cfg(target_arch = "x86_64")]
fn main() {
    extern "C" fn seven() -> usize {
        7
    }

    let result: usize;
    unsafe {
        core::arch::asm!(
            "call {}",
            sym seven,
            lateout("rax") result,
            clobber_abi("C"),
        );
    }
    assert_eq!(result, 7);
}

#[cfg(not(target_arch = "x86_64"))]
fn main() {}
