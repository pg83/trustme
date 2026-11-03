//@ compile-variant: -O
macro_rules! install {
    () => {
        fn selected() -> u32 { 67 }
    };
}

fn main() {
    install!();
    assert_eq!(selected(), 67);
}
