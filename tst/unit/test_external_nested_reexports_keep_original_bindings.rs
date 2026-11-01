//@ aux-build: reexport_path_layers.rs

use reexport_path_layers::second::{Value, extract, Branch};
use reexport_path_layers::second::nested::*;
use reexport_path_layers::first::leaf as Leaf;

fn main() {
    assert_eq!(extract(Value(257)), 257);
    assert_eq!((Branch::Left as u8, Branch::Right as u8), (3, 7));
    assert_eq!(STEP, Leaf::STEP);
    assert_eq!(STEP, 11);
}
