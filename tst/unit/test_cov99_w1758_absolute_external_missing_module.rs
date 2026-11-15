//@ aux-build: cov99_w1758_absolute_external_dep.rs
//@ compile-fail: Couldn't find path component 'absent'

fn main() {
    ::cov99_w1758_absolute_external_dep::absent::entry();
}
