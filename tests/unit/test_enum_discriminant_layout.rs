use std::mem;

enum ExplicitDiscriminants {
    Zero = 0,
    High = 0xe8,
}

fn main() {
    assert_eq!(mem::size_of::<ExplicitDiscriminants>(), 1);
    assert_eq!(ExplicitDiscriminants::Zero as u8, 0);
    assert_eq!(ExplicitDiscriminants::High as u8, 0xe8);
}
