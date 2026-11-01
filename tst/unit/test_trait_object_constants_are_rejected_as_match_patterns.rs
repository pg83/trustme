//@ compile-fail: unsized non-slice
trait Tag {}
impl Tag for u32 {}
const PATTERN: &dyn Tag = &13u32;
fn main() {
    let value: &dyn Tag = &13u32;
    match value { PATTERN => (), _ => () }
}
