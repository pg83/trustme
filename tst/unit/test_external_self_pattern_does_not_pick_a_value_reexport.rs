//@ aux-build: external_self_pattern_namespaces.rs
//@ compile-fail: expected unit struct

trait IsBaseline {
    fn is_baseline(self) -> bool;
}

impl IsBaseline for external_self_pattern_namespaces::Reading {
    fn is_baseline(self) -> bool {
        match self {
            Self => true,
            _ => false,
        }
    }
}

fn main() {}
