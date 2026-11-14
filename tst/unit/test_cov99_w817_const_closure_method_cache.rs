//@ compile-flags: -Zdump-hir

struct Probe;

impl Probe {
    fn width(self) -> usize {
        3
    }
}

const WIDTH: usize = {
    let _unused = || Probe.width();
    3
};

fn main() {
    assert!(WIDTH == 3);
}
