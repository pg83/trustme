trait Choice<const LENGTH: usize, const TAG: u8> {
    fn value() -> usize;
}

struct Catalog;

impl Choice<17, 3> for Catalog {
    fn value() -> usize { 29 }
}

impl Choice<23, 7> for Catalog {
    fn value() -> usize { 31 }
}

fn choose<const LENGTH: usize, const TAG: u8>() -> usize
where
    Catalog: Choice<LENGTH, TAG>,
{
    LENGTH + <Catalog as Choice<LENGTH, TAG>>::value()
}

fn main() {
    assert_eq!(choose::<_, 7>(), 54);
    assert_eq!(choose::<_, 3>(), 46);
    assert_eq!(choose::<_, 7>(), 54);
}
