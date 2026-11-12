#![feature(never_patterns)]

enum Empty {}

fn require_callable(_: &impl Fn(Empty) -> u16) {}

fn main() {
    assert!({
        let uncallable = |!: Empty| 1103u16;
        require_callable(&uncallable);
        true
    });
}
