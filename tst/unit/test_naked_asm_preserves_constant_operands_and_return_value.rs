#[unsafe(naked)]
extern "C" fn answer() -> u64 {
    std::arch::naked_asm!("mov rax, {value}", "ret", value = const 37);
}
#[unsafe(naked)]
extern "C" fn att_answer() -> u32 {
    std::arch::naked_asm!("movl ${value}, %eax", "retq", value = const 41, options(att_syntax));
}
fn main() { assert_eq!((answer(), att_answer()), (37, 41)); }
