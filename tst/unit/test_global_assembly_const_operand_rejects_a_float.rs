//@ compile-fail: error

core::arch::global_asm!(".byte {value}", value = const 1.5f32);

fn main() {}
