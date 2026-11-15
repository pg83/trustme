//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=mir

pub struct DropItem;

impl Drop for DropItem {
    fn drop(&mut self) {}
}

pub fn split(values: [DropItem; 32], pick_first: bool) {
    if pick_first {
        let [_first, ..] = values;
    } else {
        let [.., _last] = values;
    }
}
