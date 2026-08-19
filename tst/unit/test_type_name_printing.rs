//@ run-pass
// `type_name` prints the type the way rustc writes it, not the way the
// compiler's debug formatter does: no trailing comma in a tuple or an argument
// list, no `extern "Rust"` on an ordinary function pointer, a function item
// under the path that names it, a closure under the item that holds it, and a
// trait object with the arguments its principal trait was given.

use std::any::type_name;
use std::fmt::Debug;

#[derive(Debug)]
struct Foo;

impl Foo {
    fn new() -> Self {
        Foo
    }
}

trait Tr {
    type Bar;
}

fn opaque() -> impl Debug {
    Foo
}

fn name_of<T>(_: T) -> &'static str {
    type_name::<T>()
}

fn main() {
    assert_eq!(type_name::<(i32, u32)>(), "(i32, u32)");
    assert_eq!(type_name::<()>(), "()");
    assert_eq!(type_name::<&(i32, u32)>(), "&(i32, u32)");
    assert_eq!(type_name::<[(u8, u8); 4]>(), "[(u8, u8); 4]");
    assert_eq!(type_name::<*const (u8, u8)>(), "*const (u8, u8)");
    assert_eq!(type_name::<fn(i32) -> u8>(), "fn(i32) -> u8");
    assert_eq!(type_name::<fn(i32)>(), "fn(i32)");
    assert_eq!(type_name::<unsafe fn()>(), "unsafe fn()");
    assert_eq!(type_name::<extern "C" fn()>(), "extern \"C\" fn()");

    assert_eq!(name_of(opaque()), "test_type_name_printing::Foo");
    assert_eq!(name_of(Foo::new), "test_type_name_printing::Foo::new");
    assert_eq!(
        name_of(<Foo as Debug>::fmt),
        "<test_type_name_printing::Foo as core::fmt::Debug>::fmt"
    );
    assert_eq!(
        name_of(|| {}),
        "test_type_name_printing::main::{{closure}}"
    );

    assert_eq!(
        type_name::<dyn Tr<Bar = i32> + Send>(),
        "dyn test_type_name_printing::Tr<Bar = i32> + core::marker::Send"
    );
    assert_eq!(
        type_name::<dyn Fn(i32, i32) -> i32>(),
        "dyn core::ops::function::Fn(i32, i32) -> i32"
    );
}
