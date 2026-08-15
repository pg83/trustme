trait Zero {
    const ZERO: Self;
}

impl Zero for usize {
    const ZERO: Self = 0;
}

#[derive(PartialEq, Eq)]
struct Wrapper<T>(T);

impl<T: Zero> Zero for Wrapper<T> {
    const ZERO: Self = Wrapper(T::ZERO);
}

fn is_zero(value: Wrapper<usize>) -> bool {
    match value {
        Zero::ZERO => true,
        _ => false,
    }
}

fn main() {
    assert!(is_zero(Wrapper(0)));
    assert!(!is_zero(Wrapper(1)));
}
