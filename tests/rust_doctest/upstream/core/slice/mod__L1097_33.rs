// Extracted from library/core/src/slice/mod.rs:1097
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    
    let mut array = ['R', 'u', 's', 't', ' ', '2', '0', '1', '5'];
    let slice = &mut array[..];
    let slice_of_cells: &[Cell<char>] = Cell::from_mut(slice).as_slice_of_cells();
    for w in slice_of_cells.windows(3) {
        Cell::swap(&w[0], &w[2]);
    }
    assert_eq!(array, ['s', 't', ' ', '2', '0', '1', '5', 'u', 'R']);
}
