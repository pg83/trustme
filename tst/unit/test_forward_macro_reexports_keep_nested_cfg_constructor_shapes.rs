#![feature(decl_macro)]
mod generators {
    pub(crate) use self::create as shape;
    pub(crate) macro create($name:ident, $field:ident) {
        struct $name {
            #[cfg_attr(all(), cfg(any()))]
            removed: Missing,
            $field: u32,
        }
    }
}
use generators::shape;
shape!(Record, value);
fn main() { assert_eq!(Record { value: 97 }.value, 97); }
