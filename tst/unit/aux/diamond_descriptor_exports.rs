pub trait Base { fn read(&self) -> u16; }
pub trait Left: Base {
    fn left(&self) -> u16 { <Self as Base>::read(self) + 1 }
}
pub trait Right: Base {
    fn right(&self) -> u16 { <Self as Base>::read(self) * 2 }
}
pub trait Joined: Left + Right {}
pub trait Number { const N: u16 = 7; }
pub fn total<T: Base + Number>(value: &T) -> u16 {
    <T as Base>::read(value) + <T as Number>::N
}
