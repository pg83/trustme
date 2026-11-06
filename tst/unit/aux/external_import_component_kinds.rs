#![feature(decl_macro)]
pub mod hub {
    pub mod leaf { pub const VALUE: u8 = 31; }
    pub struct Marker;
    pub trait Shape { type Item; }
    pub use self::Shape as AliasShape;
    pub enum Choice { First }
}
pub macro check_first() {{
    use $crate::hub::Choice::First;
    match First { First => 17u8 }
}}
