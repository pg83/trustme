#![feature(decl_macro)]
fn main() {
    mod hidden { pub const CODE: u16 = 17; }
    macro read() { hidden::CODE }
    {
        mod hidden { pub const CODE: u16 = 99; }
        assert_eq!(read!(), 17);
        assert_eq!(hidden::CODE, 99);
    }
}
