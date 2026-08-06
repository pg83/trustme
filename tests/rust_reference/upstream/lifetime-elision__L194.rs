// Extracted from src/lifetime-elision.md:194
#![allow(unused)]
fn main() {
    struct Foo;
    struct Bar;
    struct Baz;
    fn somefunc(a: &Foo, b: &Bar, c: &Baz) -> usize {42}
    // Resolved as `for<'a> fn(&'a str) -> &'a str`.
    const RESOLVED_SINGLE: fn(&str) -> &str = |x| x;
    
    // Resolved as `for<'a, 'b, 'c> Fn(&'a Foo, &'b Bar, &'c Baz) -> usize`.
    const RESOLVED_MULTIPLE: &dyn Fn(&Foo, &Bar, &Baz) -> usize = &somefunc;
}
