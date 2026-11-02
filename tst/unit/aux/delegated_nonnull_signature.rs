#![feature(pattern_types, pattern_type_macro)]
pub type Present = std::pat::pattern_type!(*const u32 is !null);
pub trait Read { fn read(&self, value: Present) -> u32 { unsafe { *std::mem::transmute::<Present, *const u32>(value) } } }
pub struct Source;
impl Read for Source {}
