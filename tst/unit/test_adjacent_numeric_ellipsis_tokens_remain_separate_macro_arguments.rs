macro_rules! inclusive_count {
    ($start:literal ... $end:literal) => { $end - $start + 1usize };
}

fn main() {
    const COUNT: usize = inclusive_count!(7...13);
    let values = [29u8; COUNT];
    assert_eq!(values.len(), 7);
    assert_eq!(inclusive_count!(41...41), 1);
}
