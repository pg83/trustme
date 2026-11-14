//@ proc-macro-aux-build: cov99_w108_late_core_derive.rs

#[derive(cov99_w108_late_core_derive::W108LateCore)]
struct Marker;

fn require_copy<T: W108Copy>() {}

fn main() {
    require_copy::<u8>();
}
