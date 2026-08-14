use std::arch::{asm, naked_asm};

#[unsafe(naked)]
unsafe extern "C" fn answer() {
    naked_asm!("mov rax, {}", "ret", const 42u64);
}

fn main() {
    let value: u64;
    unsafe {
        asm!("call {}", sym answer, lateout("rax") value);
    }
    assert_eq!(value, 42);
}
