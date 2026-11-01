//@ edition: 2015
//@ proc-macro-aux-build: late_body_identity.rs

extern crate late_body_identity;

#[late::keep]
fn answer(mut value: usize) -> usize {
    unsafe {
        std::arch::asm!(
            "add {value}, 5",
            value = inout(reg) value,
            options(nomem, nostack),
        );
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
    assert_eq!(answer(37), 42);
}
