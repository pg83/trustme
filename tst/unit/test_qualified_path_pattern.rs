// A qualified path names a type in a pattern the same way it does anywhere
// else: the pattern matches the struct the associated type resolves to.
#![feature(more_qualified_paths)]

struct StructStruct {
    br: i8,
}

struct Foo;

trait A {
    type Assoc;
}

impl A for Foo {
    type Assoc = StructStruct;
}

fn main() {
    let <Foo as A>::Assoc { br } = <Foo as A>::Assoc { br: 2 };
    assert_eq!(br, 2);
}
