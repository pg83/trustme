use std::arch::global_asm;
#[no_mangle]
pub static CFG_ASM_VALUE: u64 = 89;
global_asm!(
    ".pushsection .data",
    ".balign 8",
    ".global cfg_asm_table",
    "cfg_asm_table:",
    ".quad {length}",
    ".quad {value}",
    ".popsection",
    length = const 2 + 1,
    value = sym CFG_ASM_VALUE,
);
extern "C" { static cfg_asm_table: [usize; 2]; }
fn main() {
    assert_eq!(unsafe { cfg_asm_table[0] }, 3);
    assert_eq!(unsafe { cfg_asm_table[1] }, &CFG_ASM_VALUE as *const u64 as usize);
    assert_eq!(unsafe { *(cfg_asm_table[1] as *const u64) }, 89);
}
