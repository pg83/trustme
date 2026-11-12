mod lane {
    pub struct Pair(pub(in crate) u16, pub(self) u16);

    pub fn make() -> Pair {
        Pair(829, 839)
    }

    pub fn second(value: &Pair) -> u16 {
        value.1
    }
}

fn main() {
    let pair = lane::make();
    assert_eq!(pair.0, 829);
    assert_eq!(lane::second(&pair), 839);
}
