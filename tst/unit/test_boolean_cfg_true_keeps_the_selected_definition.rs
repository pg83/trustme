#![feature(cfg_boolean_literals)]

#[cfg(true)]
fn selected() -> u16 {
    73
}

#[cfg(false)]
fn selected() -> u16 {
    missing_definition()
}

fn main() {
    assert_eq!(selected(), 73);
}
