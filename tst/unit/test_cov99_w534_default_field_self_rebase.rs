//@ compile-only
//@ compile-flags: -Zstop-after=hir -Zdump-hir
#![feature(default_field_values)]

struct Link {
    next: *const Self = 0 as *const Link,
}

fn main() {}
