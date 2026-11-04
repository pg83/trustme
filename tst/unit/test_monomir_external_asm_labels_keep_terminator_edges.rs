//@ aux-build: external_asm_label_choice.rs
//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=2
//@ compile-variant: -O

pub fn classify(value: u32) -> u32 {
    external_asm_label_choice::select(value)
}
