use std::ops::Deref;

struct Foo<T>(T);
impl<T> Deref for Foo<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }

#[derive(Clone, Copy)]
struct Bar(i32);
impl Bar { fn cake(self) -> i32 { self.0 + 1 } }

pub fn main() {
    let foo = Foo(Bar(123));
    let _bar: Bar = *foo;
    let _cake_result = foo.cake();
}
