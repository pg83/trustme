/* ppv-lite86 / rand_chacha: `Mach::u32x4x4::transpose4(a, b, c, d)` on the rigid `<M as
   Machine>::u32x4x4` - `transpose4` comes from `Vec4Ext<M::u32x4>`, a supertrait of the
   associated type's own bound `u32x4x4<M>`; the goal `<M as Machine>::u32x4x4: Vec4Ext<?W>`
   has that item-bound candidate; the `u32x4` the candidate names is a projection whose own
   bound `u32x4<M>` is not a question to ask of the goal's still-open parameter. */
trait Vec4Ext<W> {
    fn transpose4(a: Self, b: Self, c: Self, d: Self) -> (Self, Self, Self, Self)
    where
        Self: Sized;
}

trait U32x4x4<M: Machine>: Vec4Ext<M::U32x4> + Copy {}

trait Lanes<M: Machine>: Copy {}

trait Machine: Sized + Copy {
    type U32x4: Lanes<Self>;
    type U32x4x4: U32x4x4<Self>;
}

fn shuffle<M: Machine>(_m: M, a: M::U32x4x4) -> (M::U32x4x4, M::U32x4x4, M::U32x4x4, M::U32x4x4) {
    M::U32x4x4::transpose4(a, a, a, a)
}

#[derive(Clone, Copy)]
struct Soft;
#[derive(Clone, Copy, Debug, PartialEq)]
struct V([u32; 4]);
#[derive(Clone, Copy, Debug, PartialEq)]
struct V4([V; 4]);
impl Vec4Ext<V> for V4 {
    fn transpose4(a: Self, b: Self, c: Self, d: Self) -> (Self, Self, Self, Self) {
        (V4([a.0[0], b.0[0], c.0[0], d.0[0]]), a, b, c)
    }
}
impl U32x4x4<Soft> for V4 {}
impl Lanes<Soft> for V {}
impl Machine for Soft {
    type U32x4 = V;
    type U32x4x4 = V4;
}

fn main() {
    let v = V4([V([1, 2, 3, 4]); 4]);
    let (a, _, _, _) = shuffle(Soft, v);
    assert_eq!(a, v);
}
