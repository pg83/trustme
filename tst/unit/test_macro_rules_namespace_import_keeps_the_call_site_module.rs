//@ edition: 2018
#![feature(decl_macro)]

#[macro_use]
mod definition {
    pub mod target {
        pub macro value() { 617u16 }
    }
    macro_rules! bring {
        ($alias:ident) => { use target as $alias; };
    }
}

mod target {
    pub macro value() { 619u16 }
}

bring!(Imported);

fn main() {
    assert_eq!(Imported::value!(), 619);
    assert_eq!(definition::target::value!(), 617);
}
