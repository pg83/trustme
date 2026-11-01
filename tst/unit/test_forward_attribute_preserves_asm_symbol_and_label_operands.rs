//@ proc-macro-aux-build: late_body_identity.rs
#![feature(asm_goto_with_outputs)]

static ANCHOR: u8 = 19;

#[late::keep]
fn locate() -> (usize, u8) {
    let address: usize;
    let mut jumped = 0;
    unsafe {
        core::arch::asm!(
            "lea {address}, [rip + {symbol}]",
            "jmp {done}",
            address = out(reg) address,
            symbol = sym ANCHOR,
            done = label { jumped = 7; },
            options(nomem, nostack, preserves_flags),
        );
    }
    (address, jumped)
}

mod late {
    macro_rules! export {
        () => { pub use late_body_identity::keep; };
    }
    export!();
}

fn main() {
    assert_eq!(locate(), (&ANCHOR as *const u8 as usize, 7));
}
