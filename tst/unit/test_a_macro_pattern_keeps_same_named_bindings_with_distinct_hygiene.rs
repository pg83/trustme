#![feature(decl_macro)]

macro pair($caller:ident) {
    {
        let ($caller, value) = (17u8, 29u8);
        assert_eq!($caller, 17);
        assert_eq!(value, 29);
        ($caller, value)
    }
}

fn main() {
    let value = pair!(value);
    assert_eq!(value, (17, 29));
}
