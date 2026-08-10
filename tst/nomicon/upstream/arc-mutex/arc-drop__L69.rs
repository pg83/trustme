// Extracted from src/arc-mutex/arc-drop.md:69
#![allow(unused)]
fn main() {
    use std::sync::atomic::Ordering;
    use std::sync::atomic;
    atomic::fence(Ordering::Acquire);
}
