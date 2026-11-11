struct Packet(u16);

macro_rules! build {
    () => { $crate::Packet(7) };
}

#[macro_export]
macro_rules! build {
    () => { $crate::Packet(11) };
}

mod alias {
    pub use crate::build as reused;
}

fn main() {
    assert_eq!(build!().0, 11);
    assert_eq!(crate::build!().0, 11);
    assert_eq!(alias::reused!().0, 11);
}
