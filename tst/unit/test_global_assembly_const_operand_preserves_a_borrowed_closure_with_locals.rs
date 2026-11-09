std::arch::global_asm!(
    ".globl borrowed_closure_width",
    ".type borrowed_closure_width,@function",
    "borrowed_closure_width:",
    "mov rax, {value}",
    "ret",
    value = const {
        let callback = &|| {
            let local = 43usize;
            local
        };
        std::mem::size_of_val(callback) + 47usize
    },
);

extern "C" {
    fn borrowed_closure_width() -> usize;
}

fn main() {
    assert_eq!(unsafe { borrowed_closure_width() }, 47);
}
