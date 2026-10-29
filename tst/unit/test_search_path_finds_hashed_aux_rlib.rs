//@ aux-build: searched_values.rs
//@ aux-search: searched_values
extern crate searched_values;
fn main() {
    let value = searched_values::Packet([13, 21, 34]);
    assert_eq!(searched_values::checksum(value), 68);
    assert_eq!(searched_values::END, 89);
}
