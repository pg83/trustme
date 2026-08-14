#![feature(return_type_notation)]

trait Marker {}
impl Marker for () {}

trait Factory {
    fn make() -> impl Sized;
}

struct Works;

impl Factory for Works {
    #[allow(refining_impl_trait)]
    fn make() -> () {}
}

fn require_marker<T: Factory<make(..): Marker>>() {}

fn main() {
    require_marker::<Works>();
}
