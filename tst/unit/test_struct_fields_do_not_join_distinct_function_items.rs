//@ compile-fail: Type mismatch
struct Pair<F> { left:F, right:F }
struct Ops;
impl Ops { fn first(value:u8)->u16 { value as u16 } fn second(value:u8)->u16 { value as u16+1 } }
fn main() { let _=Pair{left:Ops::first,right:Ops::second}; }
