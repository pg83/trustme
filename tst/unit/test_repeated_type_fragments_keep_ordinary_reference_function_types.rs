macro_rules! aliases {
    ($first:ident, $second:ident, $ty:ty) => {
        type $first = $ty;
        type $second = $ty;
    };
}

aliases!(First, Second, fn(&mut [u16; 2]) -> &u16);

fn increment(value: &mut [u16; 2]) -> &u16 {
    value[1] += value[0];
    &value[1]
}

fn main() {
    let mut value = [3, 5];
    let first: First = increment;
    let second: Second = increment;
    assert_eq!(*first(&mut value), 8);
    assert_eq!(*second(&mut value), 11);
}
