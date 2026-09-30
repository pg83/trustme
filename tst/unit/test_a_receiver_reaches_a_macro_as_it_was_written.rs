//@ run-pass
//@ proc-macro-aux-build: receiver_shorthand.rs
// curve25519-dalek's `#[unsafe_target_feature("avx2")] impl PartialEq for
// u64x4 { fn eq(&self, ..) }` refuses a receiver with a colon: syn reads
// `&self` as a `Receiver` without one. The macro is handed the tokens as
// written; we re-printed every receiver as `self: &Self`.
extern crate receiver_shorthand;

use receiver_shorthand::receivers_are_shorthand;

struct Counter(u32);

#[receivers_are_shorthand]
impl Counter {
    fn get(&self) -> u32 {
        self.0
    }
    fn bump(&mut self) {
        self.0 += 1;
    }
    fn get_for<'a>(&'a self) -> &'a u32 {
        &self.0
    }
    fn into_inner(self) -> u32 {
        self.0
    }
    fn doubled(mut self) -> u32 {
        self.0 *= 2;
        self.0
    }
}

impl Counter {
    fn boxed(self: Box<Self>) -> u32 {
        self.0
    }
}

fn main() {
    let mut counter = Counter(1);
    counter.bump();
    assert_eq!(counter.get(), 2);
    assert_eq!(*counter.get_for(), 2);
    assert_eq!(Counter(3).into_inner(), 3);
    assert_eq!(Counter(4).doubled(), 8);
    assert_eq!(Box::new(Counter(5)).boxed(), 5);
}
