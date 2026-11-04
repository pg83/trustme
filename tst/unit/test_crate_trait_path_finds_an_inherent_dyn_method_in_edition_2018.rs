//@ edition: 2018
//@ compile-variant: -O
#![allow(bare_trait_objects)]
trait Reading { fn value(&self) -> u16; }
impl dyn Reading {
    fn doubled(&self) -> u16 { self.value() + self.value() }
}
struct Sensor(u16);
impl Reading for Sensor { fn value(&self) -> u16 { self.0 } }
fn main() {
    let sensor = Sensor(41);
    assert_eq!(crate::Reading::doubled(&sensor), 82);
}
