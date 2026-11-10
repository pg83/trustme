#![feature(decl_macro)]

macro_rules! carry { ($definition:item) => { $definition }; }

carry! { macro twice($value:expr) { $value + $value } }
carry! { macro three { () => { 3u8 } } }

fn main() {
    assert_eq!(twice!(21u16), 42);
    assert_eq!(three!(), 3);
}
