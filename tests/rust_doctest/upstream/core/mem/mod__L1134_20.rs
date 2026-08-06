// Extracted from library/core/src/mem/mod.rs:1134
#![allow(unused)]
fn main() {
    enum Enum {
        Foo,
        Bar,
        Baz,
    }
    
    assert_eq!(0, Enum::Foo as isize);
    assert_eq!(1, Enum::Bar as isize);
    assert_eq!(2, Enum::Baz as isize);
}
