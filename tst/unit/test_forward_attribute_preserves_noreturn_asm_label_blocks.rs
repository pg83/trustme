//@ proc-macro-aux-build: late_body_identity.rs

#[late::keep]
fn exit() -> u16 {
    unsafe {
        core::arch::asm!(
            "jmp {done}",
            done = label { return 42; },
            options(noreturn, nomem, nostack),
        );
    }
}

mod late {
    macro_rules! export {
        () => { pub use late_body_identity::keep; };
    }
    export!();
}

fn main() {
    assert_eq!(exit(), 42);
}
