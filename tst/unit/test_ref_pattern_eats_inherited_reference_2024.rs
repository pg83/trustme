//@ edition: 2024

#![feature(ref_pat_eat_one_layer_2024)]
#![allow(incomplete_features)]

fn main() {
    if let Some(&mut value) = &mut Some(42_u32) {
        assert_eq!(value, 42);
    } else {
        panic!();
    }
}
