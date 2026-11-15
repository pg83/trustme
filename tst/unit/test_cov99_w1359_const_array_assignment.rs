//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata

struct Factory;

trait Make<T> {
    fn make() -> T;
}

impl<const N: usize> Make<&'static [u8; N]> for Factory {
    fn make() -> &'static [u8; N] {
        &[0u8; N]
    }
}

fn make<T>() -> T
where
    Factory: Make<T>,
{
    Factory::make()
}

pub fn construct() -> usize {
    let mut array = make();
    array = &[7u8; 3];
    array.len()
}
