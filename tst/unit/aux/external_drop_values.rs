use std::sync::atomic::{AtomicU32, Ordering};

static DROPS: AtomicU32 = AtomicU32::new(0);

pub fn recorded() -> u32 { DROPS.load(Ordering::Relaxed) }

pub trait Value { fn value(&self) -> u32; }

pub struct Record(pub u32);
impl Value for Record { fn value(&self) -> u32 { self.0 } }
impl Drop for Record {
    fn drop(&mut self) { DROPS.fetch_add(self.0, Ordering::Relaxed); }
}

pub enum Tagged { Small(u32), Large(u32, u32) }
impl Value for Tagged {
    fn value(&self) -> u32 {
        match self { Self::Small(value) => *value, Self::Large(a, b) => a + b }
    }
}
impl Drop for Tagged {
    fn drop(&mut self) { DROPS.fetch_add(self.value(), Ordering::Relaxed); }
}

pub union Slot { pub number: u32, pub bits: [u8; 4] }
impl Value for Slot { fn value(&self) -> u32 { unsafe { self.number } } }
impl Drop for Slot {
    fn drop(&mut self) { DROPS.fetch_add(self.value(), Ordering::Relaxed); }
}

static mut STORAGE: Record = Record(17);
pub fn storage() -> *mut Record { std::ptr::addr_of_mut!(STORAGE) }
