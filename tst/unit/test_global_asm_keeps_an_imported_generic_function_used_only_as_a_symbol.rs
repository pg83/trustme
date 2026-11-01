//@ aux-build: asm_operand_exports.rs
//@ compile-variant: -O
core::arch::global_asm!(
    ".pushsection .data.rel.ro, \"aw\"", ".globl TRUSTME_ASM_FUNCTION",
    "TRUSTME_ASM_FUNCTION:", ".quad {function}",
    ".globl TRUSTME_ASM_DATA", "TRUSTME_ASM_DATA:", ".quad {data}",
    ".globl TRUSTME_ASM_TAG", "TRUSTME_ASM_TAG:", ".long {tag}", ".popsection",
    function = sym asm_operand_exports::only_in_asm::<u32>,
    data = sym asm_operand_exports::DATA,
    tag = const (std::mem::size_of::<u32>() + 3),
);
unsafe extern "C" {
    static TRUSTME_ASM_FUNCTION: extern "C" fn() -> usize;
    static TRUSTME_ASM_DATA: usize;
    static TRUSTME_ASM_TAG: u32;
}
fn main() {
    assert_eq!(unsafe { TRUSTME_ASM_FUNCTION }(), 33);
    assert_eq!(unsafe { TRUSTME_ASM_DATA }, &asm_operand_exports::DATA as *const u32 as usize);
    assert_eq!(unsafe { TRUSTME_ASM_TAG }, 7);
}
