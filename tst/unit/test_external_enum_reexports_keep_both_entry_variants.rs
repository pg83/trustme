use std::collections::BTreeMap;
use std::collections::btree_map::Entry::{Occupied, Vacant};

fn main() {
    let mut values = BTreeMap::<u8, u16>::new();
    match values.entry(3) {
        Vacant(slot) => { slot.insert(283); }
        Occupied(_) => panic!("entry was already occupied"),
    }
    assert_eq!(values[&3], 283);
    match values.entry(3) {
        Occupied(mut slot) => { slot.insert(285); }
        Vacant(_) => panic!("inserted entry is vacant"),
    }
    assert_eq!(values[&3], 285);
}
