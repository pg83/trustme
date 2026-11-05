//@ compile-fail: error
fn narrow(value: u16) -> u16 { value + 1 }
fn wide(value: u32) -> u32 { value + 2 }
fn choose(flag: bool) -> fn(u16) -> u16 {
    if flag { narrow } else { wide }
}
fn main() { let _ = choose(true); }
