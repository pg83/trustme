use marker_derive::Marker;

#[derive(Marker)]
pub struct Derived;

pub fn derived_value() -> u32 {
    Derived::value()
}
