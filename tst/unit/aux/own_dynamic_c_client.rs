#[link(name = "own_dynamic_value", kind = "dylib")]
extern "C" { fn own_dynamic_shift(value: u32) -> u32; }
fn main() { assert_eq!(unsafe { own_dynamic_shift(73) }, 230); }
