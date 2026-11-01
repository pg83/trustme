//@ proc-macro-aux-build: late_body_identity.rs

#[late::keep]
fn answer() -> u16 {
    unsafe { core::arch::asm!("", options(nomem, nostack, preserves_flags)); }
    unsafe { core::arch::asm!(""); }
    42
}

mod late {
    macro_rules! export {
        () => { pub use late_body_identity::keep; };
    }
    export!();
}

fn main() {
    assert_eq!(answer(), 42);
}
