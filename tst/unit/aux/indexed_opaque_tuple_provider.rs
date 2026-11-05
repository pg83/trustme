#![feature(type_alias_impl_trait)]

pub type Pair = (impl Copy, impl Clone);

#[define_opaque(Pair)]
pub fn make() -> Pair { (71u8, 73u16) }

pub fn sizes(pair: &Pair) -> (usize, usize) {
    (std::mem::size_of_val(&pair.0), std::mem::size_of_val(&pair.1))
}
