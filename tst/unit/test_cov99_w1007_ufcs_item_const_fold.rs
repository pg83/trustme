trait Number {
    fn from_trait<const N: usize>() -> usize;
}

struct Source;

impl Source {
    fn inherent<const N: usize>() -> usize {
        N
    }

    fn opaque<const N: usize>() -> impl Copy {
        N
    }
}

impl Number for Source {
    fn from_trait<const N: usize>() -> usize {
        N
    }
}

fn free<const N: usize>() -> usize {
    N
}

fn invoke<F: FnOnce() -> usize>(f: F) -> usize {
    f()
}

fn keep<T: Copy>(value: T) -> T {
    value
}

fn main() {
    assert_eq!(invoke(free::<3>), 3);
    assert_eq!(invoke(Source::inherent::<5>), 5);
    assert_eq!(invoke(Source::from_trait::<7>), 7);
    assert_eq!(invoke(<Source as Number>::from_trait::<7>), 7);
    let _ = keep(Source::opaque::<11>());
}
