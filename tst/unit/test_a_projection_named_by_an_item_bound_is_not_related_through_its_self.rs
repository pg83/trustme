// bitvec 1.1.1's BitStore: `type Unalias: BitStore<Mem = Self::Mem>`. A method
// on `T::Unalias` proves `<T::Unalias as BitStore>::Mem == T::Mem` from that
// item bound. The left side is not rigid - the bound normalizes it - so the
// equality says nothing about `T::Unalias` and `T`.
pub trait BitStore {
    type Mem: Copy;
    type Unalias: BitStore<Mem = Self::Mem>;

    fn load_value(&self) -> Self::Mem;
    fn store_value(&mut self, value: Self::Mem);
}

impl BitStore for u8 {
    type Mem = u8;
    type Unalias = u8;

    fn load_value(&self) -> u8 {
        *self
    }

    fn store_value(&mut self, value: u8) {
        *self = value;
    }
}

pub fn load<T: BitStore>(from: &T::Unalias) {
    let _value = from.load_value();
}

pub fn copy<T: BitStore>(to: &mut [T::Unalias], from: &[T::Unalias]) {
    for (to, from) in to.iter_mut().zip(from) {
        to.store_value(from.load_value());
    }
}

fn main() {
    load::<u8>(&3);
    let mut to = [1u8, 2];
    copy::<u8>(&mut to, &[3, 4]);
    assert_eq!(to, [3, 4]);
}
