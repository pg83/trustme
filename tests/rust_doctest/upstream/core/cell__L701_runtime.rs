// Extracted from library/core/src/cell.rs:701
#![allow(unused)]
#![feature(as_array_of_cells)]
fn main() {
    use std::cell::Cell;
    
    let mut array: [i32; 3] = [1, 2, 3];
    let cell_array: &Cell<[i32; 3]> = Cell::from_mut(&mut array);
    let array_cell: &[Cell<i32>; 3] = cell_array.as_array_of_cells();
}
