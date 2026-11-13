//@ compile-flags: -Zdump-mir
//@ output-contains: _3_mir.rs asm2!(
//@ output-contains: _3_mir.rs ) -> bb

use std::arch::asm;

#[inline(never)]
fn jump_to_label() -> u32 {
    let mut value = 4;
    unsafe {
        asm!(
            "jmp {destination}",
            destination = label { value = 27; },
            options(nomem, nostack),
        );
    }
    value
}

fn main() {
    assert_eq!(jump_to_label(), 27);
}
