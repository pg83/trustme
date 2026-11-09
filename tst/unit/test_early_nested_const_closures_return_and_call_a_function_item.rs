#![feature(const_closures, const_trait_impl)]

const fn answer() -> usize {
    67
}

std::arch::global_asm!(
    ".globl nested_const_function_item_answer",
    ".type nested_const_function_item_answer,@function",
    "nested_const_function_item_answer:",
    "mov rax, {value}",
    "ret",
    value = const {
        let item = answer;
        let outer = &const move || {
            let inner = &const move || item;
            inner()
        };
        let returned = outer();
        returned()
    },
);

extern "C" {
    fn nested_const_function_item_answer() -> usize;
}

fn main() {
    assert_eq!(unsafe { nested_const_function_item_answer() }, 67);
}
