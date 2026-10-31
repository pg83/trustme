//@ compile-fail: Invalid cast to pointer
fn main() { let _ = 'λ' as *const u8; }
