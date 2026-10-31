macro_rules! either {
    ($first:pat_param | $second:pat_param, $value:expr) => {
        matches!($value, $first | $second)
    };
}
fn main() {
    assert!(either!(Some(1) | Some(3), Some(3)));
    assert!(!either!(Some(1) | Some(3), None));
    assert!(either!(&1 | &3, &1));
    assert!(either!((0, _) | (_, 4), (2, 4)));
    assert!(either!(1..=3 | 7..=9, 8));
    assert!(!either!(1..=3 | 7..=9, 6));
    assert!(either!([0, ..] | [.., 9], [2, 9]));
}
