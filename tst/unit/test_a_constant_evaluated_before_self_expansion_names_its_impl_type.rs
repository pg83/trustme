// time's `type Seconds = ri64<{ UtcDateTime::MIN.unix_timestamp() }, ..>`:
// resolving the alias's const arguments evaluates `UtcDateTime::MIN` (which is
// `Self::new(..)`) on demand, before the crate-wide pass that replaces `Self`
// with the impl's type has run. The expression then has to read `Self` as the
// impl's type as bound, and a second evaluation must read the constant's type
// the same way. Upstream resolves `Self` while lowering, so no stage sees it.
#[derive(Clone, Copy)]
pub struct Ranged<const LO: i64, const HI: i64>(i64);
impl<const LO: i64, const HI: i64> Ranged<LO, HI> {
    pub const MIN: Self = Self(LO);
    pub const fn get(self) -> i64 { self.0 }
}
type Seconds = Ranged<{ Utc::MIN.ts() }, { Utc::MAX.ts() }>;
pub struct Stamp { seconds: Seconds }
impl Stamp {
    pub const fn new(seconds: Seconds) -> Self { Self { seconds } }
    pub const fn secs(&self) -> i64 { self.seconds.get() }
}
#[derive(Clone, Copy)] pub struct Plain(i64);
impl Plain { pub const fn new(v: i64) -> Self { Plain(v) } }
#[derive(Clone, Copy)] pub struct Utc { inner: Plain }
impl Utc {
    pub const MIN: Self = Self::new(-5);
    pub const MAX: Self = Self::new(5);
    pub const fn new(v: i64) -> Self { Self { inner: Plain::new(v) } }
    pub const fn ts(self) -> i64 { self.inner.0 }
}
fn main() { assert_eq!(Stamp::new(Ranged(3)).secs(), 3); assert_eq!(Seconds::MIN.get(), -5); }
