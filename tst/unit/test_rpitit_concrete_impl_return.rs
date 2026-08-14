//@ crate-type: lib
#![feature(specialization)]
#![allow(incomplete_features)]

trait Trait {
    fn make() -> impl Copy;
}

struct Implementation;

impl Trait for Implementation {
    #[allow(refining_impl_trait)]
    fn make() -> u32 {
        1
    }
}

fn concrete_return_is_visible() -> u32 {
    <Implementation as Trait>::make()
}

trait GenericTrait {
    fn copy_value(&self) -> impl Copy;
}

impl<T: Copy> GenericTrait for T {
    #[allow(refining_impl_trait)]
    fn copy_value(&self) -> T {
        *self
    }
}

impl GenericTrait for i32 {}

fn inherited_refinement_is_visible() -> i32 {
    1i32.copy_value()
}
