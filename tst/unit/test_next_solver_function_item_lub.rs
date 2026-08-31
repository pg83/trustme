trait IteratorLike {
    type Item;
}

struct Filter<I>(I);

fn first<I: IteratorLike, const N: usize>(
    _: &mut Filter<I>,
) -> Result<[I::Item; N], core::array::IntoIter<I::Item, N>> {
    loop {}
}

impl<I: IteratorLike> Filter<I> {
    fn second<const N: usize>(
        &mut self,
    ) -> Result<[I::Item; N], core::array::IntoIter<I::Item, N>> {
        loop {}
    }

    fn choose<const N: usize>(&mut self) {
        let function = const {
            if core::mem::needs_drop::<I::Item>() {
                first::<I, N>
            } else {
                Self::second::<N>
            }
        };
        let _ = function(self);
    }
}

fn main() {}
