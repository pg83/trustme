#![feature(const_closures, const_trait_impl)]

std::arch::global_asm!(
    ".globl borrowed_const_closure_result",
    ".type borrowed_const_closure_result,@function",
    "borrowed_const_closure_result:",
    "mov rax, {value}",
    "ret",
    value = const {
        let callback = &const || {
            let local = 53usize;
            local
        };
        callback() + 1
    },
);

extern "C" {
    fn borrowed_const_closure_result() -> usize;
}

fn main() {
    assert_eq!(unsafe { borrowed_const_closure_result() }, 54);
}
