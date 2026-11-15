macro_rules! join_range_floats {
    ($positive:tt .. $negative:tt ..) => {
        concat!($positive, "|", -$negative)
    };
}

fn main() {
    assert_eq!(join_range_floats!(1.25..2.5..), "1.25|-2.5");
}
