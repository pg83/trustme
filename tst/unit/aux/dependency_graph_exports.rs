use std::sync::atomic::{AtomicU16, Ordering};

static DROPPED: AtomicU16 = AtomicU16::new(0);
static OFFSET: u16 = 11;
fn add(value: u16) -> u16 { value + OFFSET }
fn multiply(value: u16) -> u16 { value * 5 }
pub static CALLBACKS: [fn(u16) -> u16; 2] = [add, multiply];

pub struct Guard(pub u16);
impl Drop for Guard {
    fn drop(&mut self) { DROPPED.fetch_add(self.0, Ordering::SeqCst); }
}
pub fn dropped() -> u16 { DROPPED.load(Ordering::SeqCst) }
