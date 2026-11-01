#[inline(never)]
fn add(mut value: usize) -> usize {
    unsafe {
        core::arch::asm!(
            "/* {not_an_operand} unmatched } */",
            "add rax, 5",
            inout("rax") value,
            options(nomem, nostack, raw),
        );
    }
    value
}

fn main() {
    assert_eq!(add(37), 42);
}
