#![feature(never_patterns)]

enum Void {}
macro_rules! functions {
    ($pattern:pat) => {
        fn first($pattern: Void) -> u32 { loop {} }
        fn second($pattern: Void) -> u32 { loop {} }
    };
}
functions!((!));

fn main() {
    let callbacks: [fn(Void) -> u32; 2] = [first, second];
    assert_eq!(callbacks.len(), 2);
}
