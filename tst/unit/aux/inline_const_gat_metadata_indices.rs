pub trait Signal {
    type Payload;
    type Code<const N: usize> where Self: Sized;
    fn read(&self) -> Self::Payload;
}

pub struct Pulse(u32);

impl Signal for Pulse {
    type Payload = u32;
    type Code<const N: usize> = u8;

    fn read(&self) -> u32 {
        self.0
    }
}

static FIRST: Pulse = Pulse(41);
static SECOND: Pulse = Pulse(73);

#[inline]
pub fn first<T>(value: T) -> (T, &'static dyn Signal<Payload = u32, Code<3> = u8>) {
    (value, &FIRST)
}

#[inline]
pub fn second<T>(value: T) -> (T, &'static dyn Signal<Payload = u32, Code<5> = u8>) {
    (value, &SECOND)
}
