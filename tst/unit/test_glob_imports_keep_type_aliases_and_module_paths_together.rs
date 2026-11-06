mod origin {
    pub struct Thing(pub u32);
    pub type Alias = Thing;
    pub enum Choice { First, Second }
}
mod bridge {
    pub use crate::origin::*;
    pub mod inner { pub fn answer() -> u32 { 43 } }
}
use bridge::*;
fn main() {
    let value: Alias = Thing(41);
    assert_eq!(value.0, 41);
    assert!(matches!(Choice::Second, Choice::Second));
    assert_eq!(inner::answer(), 43);
}
