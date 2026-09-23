// anyhow's tests write `Generic::<dyn Fn::() + ::std::marker::Sync>`.
// Upstream starts a segment's arguments at `<`, `<<`, `(` or `<-`, and in a
// type path lets `::` stand before any of them (`is_args_start` in
// `parse_path_segment`), so `Fn::()` is `Fn()`. We took `::` only before `<`.
use std::fmt::Debug;
fn call(f: &dyn Fn::() -> u8) -> u8 {
    f()
}
fn show(f: Box<dyn Fn::(u8) -> String + Send>) -> String {
    f(7)
}
fn main() {
    assert_eq!(call(&|| 3), 3);
    let _: Option<Box<dyn Fn::() + Sync>> = None;
    assert_eq!(show(Box::new(|v| format!("{v:?}"))), "7");
    let _ = std::marker::PhantomData::<dyn Debug>;
}
