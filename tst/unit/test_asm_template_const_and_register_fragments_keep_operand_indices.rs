//@ compile-variant: -O
fn main() {
    let input = std::hint::black_box(347u64);
    let output: u64;
    unsafe {
        core::arch::asm!("lea {destination}, [{source} + {offset}]",
            source = in(reg) input, destination = out(reg) output, offset = const 19,
            options(nomem, nostack));
    }
    assert_eq!(output, 366);
}
