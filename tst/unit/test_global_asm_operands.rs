use std::arch::global_asm;

extern "C" fn global_asm_target() -> usize {
    37
}

global_asm!(
    ".globl call_global_asm_target",
    ".type call_global_asm_target,@function",
    "call_global_asm_target:",
    "jmp {target}",
    target = sym global_asm_target,
);

global_asm!(
    ".globl global_asm_constant",
    ".type global_asm_constant,@function",
    "global_asm_constant:",
    "mov rax, {value}",
    "ret",
    value = const 41,
);

extern "C" {
    fn call_global_asm_target() -> usize;
    fn global_asm_constant() -> usize;
}

fn main() {
    unsafe {
        assert_eq!(call_global_asm_target(), 37);
        assert_eq!(global_asm_constant(), 41);
    }
}
