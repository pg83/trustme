//@ proc-macro-aux-build: inject_extern_core.rs
extern crate inject_extern_core;

#[inject_extern_core::import_core]
struct Root(u16);

mod nested {
    #[inject_extern_core::import_core]
    pub struct Child(pub u32);
    pub fn width() -> usize { injected_core::mem::size_of::<Child>() }
}

fn main() {
    assert_eq!(injected_core::mem::size_of::<Root>(), 2);
    assert_eq!(nested::width(), 4);
    assert_eq!(Root(11).0 + nested::Child(17).0 as u16, 28);
}
