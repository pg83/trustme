//@ compile-fail: refutable pattern
async fn selected((3..=5,): (u8,)) -> u32 { 109 }
fn main() { let _future = selected((4,)); }
