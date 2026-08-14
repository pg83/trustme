use std::arch::{asm, naked_asm};

struct Functions;

impl Functions {
    #[unsafe(naked)]
    unsafe extern "C" fn answer() {
        naked_asm!("mov rax, 42", "ret");
    }
}

fn main() {
    let value: u64;
    unsafe {
        asm!("call {}", sym Functions::answer, lateout("rax") value);
    }
    assert_eq!(value, 42);
}
