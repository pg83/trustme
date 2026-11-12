macro_rules! endpoints {
    ($first:literal ... $last:literal) => {
        ($first, $last)
    };
}

fn main() {
    assert_eq!(endpoints!(1.5...3.5), (1.5, 3.5));
    assert_eq!(endpoints!(-2.25...-1.25), (-2.25, -1.25));
}
