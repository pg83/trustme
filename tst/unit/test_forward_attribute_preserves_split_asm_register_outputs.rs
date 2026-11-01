//@ proc-macro-aux-build: late_body_identity.rs

#[late::keep]
fn split(input: usize) -> (usize, usize) {
    let first: usize;
    let second: usize;
    unsafe {
        core::arch::asm!(
            "add {value}, 3",
            "mov {copy}, {value}",
            value = inout(reg) input => first,
            copy = out(reg) second,
            out("rcx") _,
            options(nomem, nostack),
        );
        core::arch::asm!(
            "add rax, 9",
            inlateout("rax") input => _,
            options(nomem, nostack),
        );
    }
    (first, second)
}

mod late {
    macro_rules! export {
        () => { pub use late_body_identity::keep; };
    }
    export!();
}

fn main() {
    assert_eq!(split(39), (42, 42));
}
