use std::arch::asm;

extern "C" fn inline_asm_target() -> usize {
    43
}

fn main() {
    let value: usize;
    unsafe {
        asm!(
            "call {}",
            sym inline_asm_target,
            lateout("rax") value,
        );
    }
    assert_eq!(value, 43);
}
