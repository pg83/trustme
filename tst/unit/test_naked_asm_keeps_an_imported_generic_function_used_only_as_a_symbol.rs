//@ aux-build: asm_operand_exports.rs
//@ compile-variant: -O
#[unsafe(naked)]
unsafe extern "C" fn jump() -> usize {
    core::arch::naked_asm!("jmp {function}", function = sym asm_operand_exports::only_in_asm::<u8>);
}
#[unsafe(naked)]
unsafe extern "C" fn increment(_value: usize) -> usize {
    core::arch::naked_asm!("lea rax, [rdi + 1]", "ret");
}
fn main() {
    assert_eq!(unsafe { jump() }, 30);
    assert_eq!(unsafe { increment(std::hint::black_box(41)) }, 42);
}
