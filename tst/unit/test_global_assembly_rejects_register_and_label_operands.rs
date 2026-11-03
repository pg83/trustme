//@ reject-flags: --cfg single => Only `sym` and `const` are allowed || global_asm
//@ reject-flags: --cfg class => Only `sym` and `const` are allowed || global_asm
//@ reject-flags: --cfg target => `label` is not allowed || global_asm
#![feature(asm_goto)]
#[cfg(single)]
std::arch::global_asm!("", in("rax") 0usize);
#[cfg(class)]
std::arch::global_asm!("{0}", in(reg) 0usize);
#[cfg(target)]
std::arch::global_asm!("{0}", label {});
fn main() { assert_eq!(std::mem::size_of::<usize>(), 8); }
