#![feature(decl_macro)]
macro_rules! define {
    ($name:ident, $attribute:meta) => {
        macro $name() {{
            #[$attribute]
            fn hidden(_: UnknownType) {}
            19u8
        }}
    };
}
macro_rules! define_value {
    ($name:ident, $attribute:meta) => {
        macro $name() {{
            #[$attribute]
            fn value() -> u8 { 23 }
            value()
        }}
    };
}
define!(disabled, cfg(any()));
define_value!(formatted, rustfmt::skip);
define_value!(documented, doc = "local value");
fn main() {
    assert_eq!(disabled!(), 19);
    assert_eq!(formatted!(), 23);
    assert_eq!(documented!(), 23);
}
