
#![feature(lang_items)]
struct Foo<A = f32>(A);

fn main() {
    let a: Foo;
    a = Foo(123f32);
}
