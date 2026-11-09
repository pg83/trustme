#![feature(const_closures, const_trait_impl)]

std::arch::global_asm!(
    ".globl inline_const_closure_result",
    ".type inline_const_closure_result,@function",
    "inline_const_closure_result:",
    "mov rax, {value}",
    "ret",
    value = const {
        let callback = const {
            const || {
                let local = 59usize;
                local
            }
        };
        callback() + 2
    },
);

extern "C" {
    fn inline_const_closure_result() -> usize;
}

fn main() {
    assert_eq!(unsafe { inline_const_closure_result() }, 61);
}
