// compiler_builtins' __rust_probestack shape: a naked function whose AT&T
// body carries its own CFI and names registers with `%`.
use std::arch::naked_asm;

#[unsafe(naked)]
pub extern "C" fn forty_two() -> u64 {
    naked_asm!(
        ".cfi_startproc",
        "pushq %rbp",
        ".cfi_adjust_cfa_offset 8",
        ".cfi_offset %rbp, -16",
        "movq %rsp, %rbp",
        ".cfi_def_cfa_register %rbp",
        "movq $42, %rax",
        "leave",
        ".cfi_def_cfa_register %rsp",
        ".cfi_adjust_cfa_offset -8",
        "ret",
        ".cfi_endproc",
        options(att_syntax)
    )
}

fn main() {
    assert_eq!(forty_two(), 42);
}
