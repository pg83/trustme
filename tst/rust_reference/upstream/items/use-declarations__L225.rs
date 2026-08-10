// Extracted from src/items/use-declarations.md:225
mod m {
    pub enum E { V1, V2 }
    pub trait Tr { fn f(&self); }
}
use m::{self as _}; // OK: Modules can be parents of `self`.
use m::E::{self, V1}; // OK: Enums can be parents of `self`.
use m::Tr::{self}; // OK: Traits can be parents of `self`.
fn main() {}
