//@ compile-fail: error

trait Dimension<const N: usize> {}
struct Local;

impl Dimension for Local {}

fn main() {}
