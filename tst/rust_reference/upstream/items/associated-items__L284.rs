// Extracted from src/items/associated-items.md:284
struct ArrayLender<'a, T>(&'a mut [T; 16]);

trait Lend {
    // Generic associated type declaration
    type Lender<'a> where Self: 'a;
    fn lend<'a>(&'a mut self) -> Self::Lender<'a>;
}

impl<T> Lend for [T; 16] {
    // Generic associated type definition
    type Lender<'a> = ArrayLender<'a, T> where Self: 'a;

    fn lend<'a>(&'a mut self) -> Self::Lender<'a> {
        ArrayLender(self)
    }
}

fn borrow<'a, T: Lend>(array: &'a mut T) -> <T as Lend>::Lender<'a> {
    array.lend()
}

fn main() {
    let mut array = [0usize; 16];
    let lender = borrow(&mut array);
}
