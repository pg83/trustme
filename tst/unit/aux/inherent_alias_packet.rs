#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

pub struct Packet<T>(pub T);

impl<T: Copy> Packet<T> {
    pub type Item = T;
    pub type Pair = (T, T);

    pub fn duplicate(self) -> Self::Pair {
        (self.0, self.0)
    }
}
