#![feature(linkage)]
#[unsafe(naked)]
#[unsafe(export_name = "trustme_ember_offset")]
unsafe extern "C" fn exported(_: u64) -> u64 {
    core::arch::naked_asm!("lea rax, [rdi + 43]", "ret");
}
#[unsafe(naked)]
#[linkage = "weak"]
unsafe extern "C" fn weak(_: u64) -> u64 {
    core::arch::naked_asm!("lea rax, [rdi + 47]", "ret");
}
fn main() {
    let first = std::hint::black_box(exported as unsafe extern "C" fn(u64) -> u64);
    let second = std::hint::black_box(weak as unsafe extern "C" fn(u64) -> u64);
    assert_eq!(unsafe { first(53) }, 96);
    assert_eq!(unsafe { second(59) }, 106);
}
