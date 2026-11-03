#[macro_use]
mod definitions {
    macro_rules! direct { () => { 193u16 }; }
    #[macro_use]
    mod nested { macro_rules! indirect { () => { 197u16 }; } }
}
fn main() { assert_eq!(direct!() + indirect!(), 390); }
