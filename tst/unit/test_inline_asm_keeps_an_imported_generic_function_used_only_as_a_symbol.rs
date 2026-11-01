//@ aux-build: asm_operand_exports.rs
//@ compile-variant: -O
fn main() {
    let address: usize;
    unsafe {
        core::arch::asm!("lea {address}, [rip + {function}]", address = out(reg) address,
                         function = sym asm_operand_exports::only_in_asm::<u16>,
                         options(nomem, nostack, preserves_flags));
        let call: extern "C" fn() -> usize = std::mem::transmute(address);
        assert_eq!(call(), 31);
    }
}
