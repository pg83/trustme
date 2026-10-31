//@ compile-fail: cannot match
fn answer() -> u32 { 37 }
fn main() {
    let function: fn() -> u32 = answer;
    match function { () => (), _ => () }
}
