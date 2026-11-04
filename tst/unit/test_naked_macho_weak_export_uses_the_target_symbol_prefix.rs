//@ compile-only
//@ compile-flags: --target=$DIR/inputs/naked_macho_codegen_target.toml -Cemit-cpp-only
//@ input: inputs/naked_macho_codegen_target.toml
#![feature(linkage)]
#[unsafe(naked)]
#[unsafe(export_name = "trustme_cinder_offset")]
#[linkage = "weak"]
pub unsafe extern "C" fn offset(_: u64) -> u64 {
    core::arch::naked_asm!("lea rax, [rdi + 61]", "ret");
}
fn main() {
    let call = std::hint::black_box(offset as unsafe extern "C" fn(u64) -> u64);
    assert_eq!(unsafe { call(67) }, 128);
}
