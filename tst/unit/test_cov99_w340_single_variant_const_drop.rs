#![feature(const_trait_impl, const_destruct)]

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
        let _envelope = Envelope::Only(Tick(&mut count));
    }
    count
}

const COUNT: u32 = dropped_count();

fn main() {
    assert_eq!(COUNT, 11);
    assert_eq!(dropped_count(), 11);
}
