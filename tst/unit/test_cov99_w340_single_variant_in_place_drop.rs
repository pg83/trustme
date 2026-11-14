#![feature(const_drop_in_place, const_trait_impl, const_destruct)]

struct Tick<'a>(&'a mut u32);

impl const Drop for Tick<'_> {
    fn drop(&mut self) {
        *self.0 += 11;
    }
}

enum Envelope<'a> {
    Only(Tick<'a>),
}

const fn dropped_count() -> u32 {
    let mut count = 0;
    {
        let mut envelope = core::mem::ManuallyDrop::new(Envelope::Only(Tick(&mut count)));
        unsafe {
            core::ptr::drop_in_place(&raw mut envelope as *mut Envelope<'_>);
        }
    }
    count
}

const COUNT: u32 = dropped_count();

fn main() {
    assert_eq!(COUNT, 11);
    assert_eq!(dropped_count(), 11);
}
