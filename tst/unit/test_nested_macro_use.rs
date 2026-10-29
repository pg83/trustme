#[macro_use]
mod outer {
    macro_rules! outer_value { () => { 13u8 }; }
    #[macro_use]
    mod inner {
        macro_rules! inner_value { () => { 17u8 }; }
    }
}
fn main() { assert_eq!(outer_value!() + inner_value!(), 30); }
