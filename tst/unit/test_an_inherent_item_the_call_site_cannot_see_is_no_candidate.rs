// crc declares a private `const fn new` in each of its `impl Digest<'a, uN, Table<L>>`
// blocks, one per module, and calls `Digest::new(self, value)` in each. rustc
// drops a candidate the call site cannot see (`consider_candidates` keeps it only
// for the privacy diagnostic), so each module's call has one `new` - its own.
// Every impl's `new` stayed a candidate and the call was left ambiguous.
trait Implementation {
    type Data<W>;
}

pub struct Table<const L: usize> {}

impl<const L: usize> Implementation for Table<L> {
    type Data<W> = [[W; 4]; L];
}

mod private {
    pub trait Sealed {}
    impl Sealed for super::Table<0> {}
    impl Sealed for super::Table<1> {}
}

pub struct Crc<W, I: Implementation = Table<1>> {
    data: I::Data<W>,
    init: W,
}

pub struct Digest<'a, W, I: Implementation = Table<1>> {
    crc: &'a Crc<W, I>,
    value: W,
}

mod crc32 {
    use super::*;
impl<const L: usize> Crc<u32, Table<L>>
where
    Table<L>: private::Sealed,
{
    pub const fn digest_with_initial(&self, initial: u32) -> Digest<'_, u32, Table<L>> {
        Digest::new(self, initial)
    }
}

impl<'a, const L: usize> Digest<'a, u32, Table<L>>
where
    Table<L>: private::Sealed,
{
    const fn new(crc: &'a Crc<u32, Table<L>>, value: u32) -> Self {
        Digest { crc, value }
    }
}

}

mod crc64 {
    use super::*;
impl<const L: usize> Crc<u64, Table<L>>
where
    Table<L>: private::Sealed,
{
    pub const fn digest_with_initial(&self, initial: u64) -> Digest<'_, u64, Table<L>> {
        Digest::new(self, initial)
    }
}

impl<'a, const L: usize> Digest<'a, u64, Table<L>>
where
    Table<L>: private::Sealed,
{
    const fn new(crc: &'a Crc<u64, Table<L>>, value: u64) -> Self {
        Digest { crc, value }
    }
}

}

fn main() {
    let c = Crc::<u32, Table<1>> { data: [[5; 4]; 1], init: 2 };
    let d = c.digest_with_initial(3);
    assert_eq!(d.value + d.crc.init + d.crc.data[0][0], 10);
    let c64 = Crc::<u64, Table<0>> { data: [], init: 4 };
    assert_eq!(c64.digest_with_initial(1).value, 1);
}
