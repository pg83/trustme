#![feature(lang_items)]

trait Foo {
    type Bar<T>;
    type Baz<'a>;
}
