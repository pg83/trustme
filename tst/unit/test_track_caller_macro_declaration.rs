#[track_caller]
macro_rules! tracked_macro {
    () => { 42 };
}

fn main() {
    assert_eq!(tracked_macro!(), 42);
}
