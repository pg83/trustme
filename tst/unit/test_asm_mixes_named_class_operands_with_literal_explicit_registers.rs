//@ compile-variant: -O
fn main() {
    let source = std::hint::black_box(307u64);
    let first: u64;
    let second: u64;
    unsafe {
        core::arch::asm!("lea {named}, [{input} + 3]", "mov rax, {input}",
            input = in(reg) source, named = out(reg) second, out("rax") first,
            options(nomem, nostack));
    }
    assert_eq!((first, second), (307, 310));
}
