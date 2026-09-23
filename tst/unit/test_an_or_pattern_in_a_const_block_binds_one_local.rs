// time's `Iso8601::<CONFIG>::compute_metadata` computes in a `const { }`
// block `match Self::TIME_PRECISION { Hour { decimal_digits } | Minute {
// decimal_digits } | Second { decimal_digits } => .. }`. The block is lifted
// into a constant of its own and its locals renumbered; each alternative of
// the or-pattern got a slot of its own for `decimal_digits` while the arm's
// body read the first one's, so evaluating the `Second` case read an
// unset local.
#[derive(Clone, Copy)]
pub enum P { Hour { digits: Option<core::num::NonZero<u8>> }, Minute { digits: Option<core::num::NonZero<u8>> }, Second { digits: Option<core::num::NonZero<u8>> } }
pub struct Iso<const C: u8>;
impl<const C: u8> Iso<C> {
    const TP: P = if C == 0 { P::Hour { digits: None } } else { P::Second { digits: core::num::NonZero::new(C) } };
    const SEP: bool = C > 1;
    fn meta(&self) -> usize {
        const {
            let num_colons = match Self::TP {
                P::Minute { .. } if Self::SEP => 1,
                P::Second { .. } if Self::SEP => 2,
                P::Hour { .. } | P::Minute { .. } | P::Second { .. } => 0,
            };
            let frac = match Self::TP {
                P::Hour { digits } | P::Minute { digits } | P::Second { digits } => {
                    if let Some(d) = digits { 1 + d.get() as usize } else { 0 }
                }
            };
            num_colons + frac
        }
    }
}
fn main() {
    assert_eq!(Iso::<3>.meta(), 6);
    assert_eq!(Iso::<0>.meta(), 0);
}
