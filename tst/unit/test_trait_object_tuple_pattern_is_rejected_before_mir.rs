//@ compile-fail: cannot match
trait Value {}
impl Value for u32 {}
fn main() {
    let object: &dyn Value = &37u32;
    match *object { (..) => (), _ => () }
}
