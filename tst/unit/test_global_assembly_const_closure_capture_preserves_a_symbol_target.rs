#![feature(const_closures, const_trait_impl)]

#[inline(never)]
extern "C" fn receive(value: usize) -> usize {
    value + 7
}

std::arch::global_asm!(
    ".globl captured_const_closure_symbol_answer",
    ".type captured_const_closure_symbol_answer,@function",
    "captured_const_closure_symbol_answer:",
    "mov edi, {value}",
    "jmp {target}",
    value = const {
        let number = 73usize;
        let capture = &const move || number;
        capture()
    },
    target = sym receive,
);

extern "C" {
    fn captured_const_closure_symbol_answer() -> usize;
}

fn main() {
    assert_eq!(unsafe { captured_const_closure_symbol_answer() }, 80);
}
