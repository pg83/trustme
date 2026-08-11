#![feature(decl_macro)]

fn definition_site_value() -> u32 {
    23
}

macro call_definition_site() {
    definition_site_value()
}

fn main() {
    assert_eq!(call_definition_site!(), 23);
}
