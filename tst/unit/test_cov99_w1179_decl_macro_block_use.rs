#![feature(decl_macro)]

mod origin {
    pub const NUMBER: u32 = 73;
}

macro read_number() {{
    use origin::NUMBER;
    NUMBER
}}

fn main() {
    assert_eq!(read_number!(), 73);
}
