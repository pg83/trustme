//@ aux-build: external_drop_values.rs
extern crate external_drop_values;
use external_drop_values::{recorded, Record, Slot, Tagged, Value};

fn main() {
    let record: Box<dyn Value> = Box::new(Record(7));
    let tagged: Box<dyn Value> = Box::new(Tagged::Large(5, 8));
    let slot: Box<dyn Value> = Box::new(Slot { number: 22 });
    assert_eq!((record.value(), tagged.value(), slot.value()), (7, 13, 22));
    drop(record);
    assert_eq!(recorded(), 7);
    drop(tagged);
    assert_eq!(recorded(), 20);
    drop(slot);
    assert_eq!(recorded(), 42);
}
