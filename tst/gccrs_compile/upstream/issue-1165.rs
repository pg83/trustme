
#![feature(lang_items)]
struct Foo<T>(T);

fn main() {
    &Foo(123);
}
