macro_rules! type_import {
    ($module:ty) => { use $module::{size_of as bytes, align_of as alignment}; };
}
macro_rules! path_import {
    ($module:path) => { use $module::{size_of as bytes, align_of as alignment}; };
}
macro_rules! type_glob { ($module:ty) => { use $module::*; }; }
macro_rules! path_glob { ($module:path) => { use $module::*; }; }
mod from_type { type_import!(std::mem); pub fn widths() -> (usize, usize) { (bytes::<u32>(), alignment::<u32>()) } }
mod from_path { path_import!(std::mem); pub fn widths() -> (usize, usize) { (bytes::<u32>(), alignment::<u32>()) } }
mod type_wildcard { type_glob!(std::mem); pub fn width() -> usize { size_of::<u16>() } }
mod path_wildcard { path_glob!(std::mem); pub fn width() -> usize { size_of::<u16>() } }
fn main() {
    assert_eq!(from_type::widths(), (4, 4));
    assert_eq!(from_path::widths(), (4, 4));
    assert_eq!((type_wildcard::width(), path_wildcard::width()), (2, 2));
}
