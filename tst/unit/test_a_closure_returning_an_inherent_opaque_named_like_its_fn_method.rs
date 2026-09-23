// mockall_derive: `.map(|f| f.call(..))` where the inherent
// `MockFunction::call` returns `impl ToTokens`. The closure's own `Fn::call`
// has the same name, but upstream ties an opaque type to the function that
// defines it (its `DefId`), not to a name: the closure's output is the
// inherent method's opaque, not an `impl Trait` of the closure's `call`.
use std::fmt::Display;
struct MockFunction(u32);
impl MockFunction {
    fn call(&self, extra: u32) -> impl Display {
        self.0 + extra
    }
}
fn render<'a>(fs: &'a [MockFunction]) -> impl Iterator<Item = String> + 'a {
    fs.iter().map(|f| f.call(1)).map(|d| d.to_string())
}
fn main() {
    let fs = [MockFunction(1), MockFunction(2)];
    let out: Vec<String> = render(&fs).collect();
    assert_eq!(out, ["2", "3"]);
    let g = |f: &MockFunction| f.call(10);
    assert_eq!(g(&fs[0]).to_string(), "11");
}
