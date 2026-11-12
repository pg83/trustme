#![feature(inherent_associated_types)]
struct Factory;
impl Factory {
    type Item<F> = F;
    fn make() -> Self::Item<impl Fn() -> u16> { || 233 }
}
fn main() { assert_eq!(Factory::make()(), 233); }
