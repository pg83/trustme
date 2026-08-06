// Extracted from library/std/src/keyword_docs.rs:1590
#![allow(unused)]
fn main() {
    struct Foo { field1: String, field2: () }
    let thing = Foo { field1: "".to_string(), field2: () };
    let updated_thing = Foo {
        field1: "a new value".to_string(),
        ..thing
    };
}
