//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs ::marker::Send+
//@ output-contains: _1_ast.rs ::marker::Sync+
//@ output-contains: _1_ast.rs 'static
//@ output-contains: _1_ast.rs for<
struct Processor {
    run: Box<dyn for<'a> Fn(&'a str) -> usize + Send + Sync + 'static>,
}
fn invoke<'env>(callback: &(dyn for<'a> Fn(&'a str) -> usize + Send + Sync + 'env), text: &str) -> usize { callback(text) }
fn main() {
    let offset = 7usize;
    let processor = Processor { run: Box::new(move |text: &str| text.chars().count() + offset) };
    assert_eq!(invoke(&*processor.run, "a🦀b"), 10);
    assert_eq!(invoke(&*processor.run, ""), 7);
}
