macro_rules! capture {
    ($value:expr) => { $value };
}

fn main() {
    let at_least = capture!(std::marker::PhantomData::<u8>>=std::marker::PhantomData::<u8>);
    let strictly_greater = capture!(std::marker::PhantomData::<u16>>std::marker::PhantomData::<u16>);
    assert!(at_least);
    assert!(!strictly_greater);
}
