//@ proc-macro-aux-build: late_body_identity.rs

#[late::keep]
fn sum(input: usize) -> usize {
    let mut total = input;
    let result: usize;
    unsafe {
        core::arch::asm!(
            "add {total}, {amount}",
            "mov {result}, {total}",
            total = inout(reg) total,
            amount = const 5usize,
            result = lateout(reg) result,
            options(nomem, nostack),
        );
    }
    assert_eq!(total, input + 5);
    result
}

mod late {
    macro_rules! export {
        () => { pub use late_body_identity::keep; };
    }
    export!();
}

fn main() {
    assert_eq!(sum(37), 42);
}
