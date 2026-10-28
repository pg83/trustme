//@ compile-fail: Item not found for
trait Required {
    fn required();
}
struct S;
impl Required for S {}
fn main() { S::required(); }
