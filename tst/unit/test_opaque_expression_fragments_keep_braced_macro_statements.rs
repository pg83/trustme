macro_rules! bump {
    ($counter:expr) => {{ $counter += 29; }};
}

macro_rules! forward {
    ($expression:expr) => {{ $expression; }};
}

fn main() {
    let mut counter = 7;
    forward!(bump!{counter});
    assert_eq!(counter, 36);
}
