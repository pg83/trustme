//@ crate-type: lib

pub fn range() {
    for quad_index in (0..4).rev() {
        let _: usize = quad_index;
    }
}
