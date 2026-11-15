//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only -Zmir-opt-level=0 -Zdump-mir
//@ output-contains: .cpp __asm__ __volatile__ goto
#![feature(never_type, asm_goto_with_outputs)]

pub unsafe fn never_vector_inout_label(mut value: !) {
    core::arch::asm!(
        "/* {value} */",
        "jmp {done}",
        value = inout(xmm_reg) value,
        done = label {},
    );
}
