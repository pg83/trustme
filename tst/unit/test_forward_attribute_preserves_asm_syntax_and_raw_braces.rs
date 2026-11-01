//@ proc-macro-aux-build: late_body_identity.rs

#[late::keep]
fn adjusted(mut value: usize) -> usize {
    unsafe {
        core::arch::asm!(
            "addq $5, {value}",
            value = inout(reg) value,
            options(pure, nomem, nostack, att_syntax),
        );
        core::arch::asm!(
            "/* raw { } quote \" slash \\ */",
            "add rax, 2",
            inlateout("rax") value,
            options(nomem, nostack, raw),
        );
        core::arch::asm!("/* escaped {{ and }} */", options(nomem, nostack, preserves_flags));
    }
    value
}

mod late {
    macro_rules! export {
        () => { pub use late_body_identity::keep; };
    }
    export!();
}

fn main() {
    assert_eq!(adjusted(35), 42);
}
