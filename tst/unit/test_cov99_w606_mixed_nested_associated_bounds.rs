//@ crate-type: lib
//@ compile-flags: --emit=metadata

trait Unrelated {}
trait Goal {}
trait Middle {
    type Leaf;
}
trait Root {
    type Branch: Unrelated + Middle<Leaf: Goal>;
}

fn needs_goal<T: Goal>() {}

pub fn check<R: Root>() {
    needs_goal::<<<R as Root>::Branch as Middle>::Leaf>();
}
