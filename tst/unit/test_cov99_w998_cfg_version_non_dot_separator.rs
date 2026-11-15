#![feature(cfg_version)]

fn main() {
    assert!(!cfg!(version("1x2")));
}
