//@ proc-macro-aux-build: late_body_identity.rs

#[late::keep]
#[unsafe(naked)]
unsafe extern "C" fn add_five(_: usize) -> usize {
    core::arch::naked_asm!("lea 5(%rdi), %rax", "ret", options(att_syntax));
}

#[late::keep]
fn load(value: &u32) -> u32 {
    let result: u32;
    unsafe {
        core::arch::asm!(
            "mov {result:e}, [{pointer}]",
            result = lateout(reg) result,
            pointer = in(reg) value,
            options(pure, readonly, nostack, preserves_flags),
        );
    }
    result
}

mod late {
    macro_rules! export {
        () => { pub use late_body_identity::keep; };
    }
    export!();
}

fn main() {
    assert_eq!(unsafe { add_five(37) }, 42);
    assert_eq!(load(&0x1234abcd), 0x1234abcd);
}
