//@ compile-fail: Failed to find an impl
trait Mark {}
impl Mark for unsafe extern "C" fn(u16) -> u32 {}
fn demand<F: Mark>() {}
fn main() { demand::<unsafe extern "C" fn(u16, ...) -> u32>(); }
