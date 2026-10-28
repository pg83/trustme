//@ compile-fail: error
trait Required {
    fn required();
}
struct S;
impl Required for S {}
fn main() {}
